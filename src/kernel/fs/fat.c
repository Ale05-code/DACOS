#include "fat.h"
#include "path.h"
#include <arch/i686/ata.h>
#include <arch/i686/rtc.h>
#include <string.h>
#include <memory.h>
#include <debug.h>

#define MODULE                  "FAT"

#define SECTOR_SIZE             512
#define ENTRY_SIZE              32
#define ENTRIES_PER_SECTOR      (SECTOR_SIZE / ENTRY_SIZE)

#define ATTR_READ_ONLY          0x01
#define ATTR_HIDDEN             0x02
#define ATTR_SYSTEM             0x04
#define ATTR_VOLUME_ID          0x08
#define ATTR_DIRECTORY          0x10
#define ATTR_ARCHIVE            0x20
#define ATTR_LFN                0x0F

#define NT_LOWERCASE_BASE       0x08    // byte 12 of the entry: Windows NT/Linux "lowercase" flags
#define NT_LOWERCASE_EXT        0x10

#define LFN_LAST                0x40
#define LFN_CHARS               13      // characters in one long-name entry
#define LFN_MAX_PARTS           20      // we read names up to 260 characters

#define DELETED_MARK            0xE5

// the kernel lives here: the shell can't modify it
#define PROTECTED_PATH          "/boot"

// --- on-disk structures --------------------------------------------------------

typedef struct
{
    uint8_t Name[11];
    uint8_t Attributes;
    uint8_t NTReserved;
    uint8_t CreatedTimeTenths;
    uint16_t CreatedTime;
    uint16_t CreatedDate;
    uint16_t AccessedDate;
    uint16_t FirstClusterHigh;
    uint16_t ModifiedTime;
    uint16_t ModifiedDate;
    uint16_t FirstClusterLow;
    uint32_t Size;
} __attribute__((packed)) DirEntry;

typedef struct
{
    uint8_t Order;
    uint16_t Name1[5];
    uint8_t Attributes;
    uint8_t Type;
    uint8_t Checksum;
    uint16_t Name2[6];
    uint16_t FirstCluster;
    uint16_t Name3[2];
} __attribute__((packed)) LFNEntry;

typedef struct
{
    uint8_t Jump[3];
    uint8_t Oem[8];
    uint16_t BytesPerSector;
    uint8_t SectorsPerCluster;
    uint16_t ReservedSectors;
    uint8_t FatCount;
    uint16_t RootEntryCount;
    uint16_t TotalSectors16;
    uint8_t Media;
    uint16_t SectorsPerFat16;
    uint16_t SectorsPerTrack;
    uint16_t Heads;
    uint32_t HiddenSectors;
    uint32_t TotalSectors32;
    union {
        struct {
            uint8_t DriveNumber;
            uint8_t Reserved;
            uint8_t Signature;
            uint32_t VolumeId;
            uint8_t VolumeLabel[11];
            uint8_t SystemId[8];
        } __attribute__((packed)) Fat16;
        struct {
            uint32_t SectorsPerFat;
            uint16_t Flags;
            uint16_t Version;
            uint32_t RootCluster;
            uint16_t FSInfoSector;
            uint16_t BackupBootSector;
            uint8_t Reserved[12];
            uint8_t DriveNumber;
            uint8_t Reserved1;
            uint8_t Signature;
            uint32_t VolumeId;
            uint8_t VolumeLabel[11];
            uint8_t SystemId[8];
        } __attribute__((packed)) Fat32;
    };
} __attribute__((packed)) BootSector;

// --- state of the mounted volume -------------------------------------------------

static bool g_Mounted = false;
static uint32_t g_PartitionLba;         // absolute LBA where the volume starts
static int g_FatType;
static uint32_t g_SectorsPerCluster;
static uint32_t g_ClusterSize;          // bytes
static uint32_t g_ReservedSectors;
static uint32_t g_FatCount;
static uint32_t g_SectorsPerFat;
static uint32_t g_RootEntryCount;       // FAT12/16
static uint32_t g_RootDirLba;           // FAT12/16, relative to the volume
static uint32_t g_RootCluster;          // FAT32
static uint32_t g_DataLba;              // first sector of cluster 2, relative
static uint32_t g_ClusterCount;         // valid clusters: 2 .. g_ClusterCount + 1
static uint32_t g_FreeClusters;
static uint32_t g_NextFree;
static uint32_t g_FSInfoSector;         // FAT32, 0 if missing
static char g_VolumeLabel[12];

// --- sector cache (write-through) ------------------------------------------------
// Every read and write goes through a small direct-mapped cache: writes are
// sent to the disk immediately, so the disk is always up to date.

#define CACHE_SIZE  32

typedef struct
{
    bool Valid;
    uint32_t Lba;
    uint8_t Data[SECTOR_SIZE];
} CacheEntry;

static CacheEntry g_Cache[CACHE_SIZE];

static void Cache_Clear()
{
    for (int i = 0; i < CACHE_SIZE; i++)
        g_Cache[i].Valid = false;
}

// lba is relative to the volume
static uint8_t* Cache_Read(uint32_t lba)
{
    CacheEntry* entry = &g_Cache[lba % CACHE_SIZE];
    if (entry->Valid && entry->Lba == lba)
        return entry->Data;

    entry->Valid = false;
    if (!ATA_ReadSectors(g_PartitionLba + lba, 1, entry->Data))
    {
        log_err(MODULE, "read error at sector %u", g_PartitionLba + lba);
        return NULL;
    }
    entry->Valid = true;
    entry->Lba = lba;
    return entry->Data;
}

static bool Cache_Write(uint32_t lba, const uint8_t* data)
{
    CacheEntry* entry = &g_Cache[lba % CACHE_SIZE];
    if (data != entry->Data)
        memcpy(entry->Data, data, SECTOR_SIZE);
    entry->Valid = true;
    entry->Lba = lba;

    if (!ATA_WriteSectors(g_PartitionLba + lba, 1, entry->Data))
    {
        log_err(MODULE, "write error at sector %u", g_PartitionLba + lba);
        entry->Valid = false;
        return false;
    }
    return true;
}

// --- the File Allocation Table ---------------------------------------------------

static uint32_t Fat_EndOfChain()
{
    return g_FatType == 12 ? 0xFFF : g_FatType == 16 ? 0xFFFF : 0x0FFFFFFF;
}

static bool Fat_IsEndOfChain(uint32_t value)
{
    return value >= (g_FatType == 12 ? 0xFF8u : g_FatType == 16 ? 0xFFF8u : 0x0FFFFFF8u);
}

static bool Cluster_IsValid(uint32_t cluster)
{
    return cluster >= 2 && cluster < g_ClusterCount + 2;
}

static uint32_t Cluster_ToLba(uint32_t cluster)
{
    return g_DataLba + (cluster - 2) * g_SectorsPerCluster;
}

// byte offset of the FAT entry of a cluster, from the start of the FAT
static uint32_t Fat_EntryOffset(uint32_t cluster)
{
    if (g_FatType == 12) return cluster + cluster / 2;     // 1.5 bytes per entry
    if (g_FatType == 16) return cluster * 2;
    return cluster * 4;
}

static int Fat_EntryBytes()
{
    return g_FatType == 32 ? 4 : 2;     // FAT12 entries are spread over 2 bytes
}

static bool Fat_ReadBytes(uint32_t offset, uint8_t* out, int count)
{
    for (int i = 0; i < count; i++)
    {
        uint8_t* sector = Cache_Read(g_ReservedSectors + (offset + i) / SECTOR_SIZE);
        if (sector == NULL)
            return false;
        out[i] = sector[(offset + i) % SECTOR_SIZE];
    }
    return true;
}

static bool Fat_Get(uint32_t cluster, uint32_t* value)
{
    uint8_t b[4] = { 0 };
    if (!Fat_ReadBytes(Fat_EntryOffset(cluster), b, Fat_EntryBytes()))
        return false;

    if (g_FatType == 12)
    {
        uint32_t v = b[0] | (b[1] << 8);
        *value = (cluster & 1) ? (v >> 4) : (v & 0x0FFF);
    }
    else if (g_FatType == 16)
        *value = b[0] | (b[1] << 8);
    else
        *value = (b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24)) & 0x0FFFFFFF;
    return true;
}

// writes the entry in every copy of the FAT
static bool Fat_Set(uint32_t cluster, uint32_t value)
{
    uint32_t offset = Fat_EntryOffset(cluster);
    int count = Fat_EntryBytes();
    uint8_t b[4];
    if (!Fat_ReadBytes(offset, b, count))
        return false;

    if (g_FatType == 12)
    {
        if (cluster & 1)
        {
            b[0] = (b[0] & 0x0F) | ((value << 4) & 0xF0);
            b[1] = (value >> 4) & 0xFF;
        }
        else
        {
            b[0] = value & 0xFF;
            b[1] = (b[1] & 0xF0) | ((value >> 8) & 0x0F);
        }
    }
    else if (g_FatType == 16)
    {
        b[0] = value & 0xFF;
        b[1] = (value >> 8) & 0xFF;
    }
    else
    {
        // FAT32: the upper 4 bits are reserved and must be preserved
        uint32_t old = b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24);
        uint32_t v = (old & 0xF0000000) | (value & 0x0FFFFFFF);
        b[0] = v & 0xFF;
        b[1] = (v >> 8) & 0xFF;
        b[2] = (v >> 16) & 0xFF;
        b[3] = (v >> 24) & 0xFF;
    }

    for (uint32_t copy = 0; copy < g_FatCount; copy++)
    {
        uint32_t base = g_ReservedSectors + copy * g_SectorsPerFat;
        uint32_t currentLba = 0xFFFFFFFF;
        uint8_t* sector = NULL;

        for (int i = 0; i < count; i++)
        {
            uint32_t lba = base + (offset + i) / SECTOR_SIZE;
            if (lba != currentLba)
            {
                if (sector != NULL && !Cache_Write(currentLba, sector))
                    return false;
                sector = Cache_Read(lba);
                if (sector == NULL)
                    return false;
                currentLba = lba;
            }
            sector[(offset + i) % SECTOR_SIZE] = b[i];
        }
        if (!Cache_Write(currentLba, sector))
            return false;
    }
    return true;
}

static bool Cluster_Zero(uint32_t cluster)
{
    uint8_t zero[SECTOR_SIZE];
    memset(zero, 0, sizeof(zero));
    for (uint32_t s = 0; s < g_SectorsPerCluster; s++)
        if (!Cache_Write(Cluster_ToLba(cluster) + s, zero))
            return false;
    return true;
}

// Finds a free cluster, marks it as end of chain and links it after "previous" (if != 0)
static FAT_Status Cluster_Allocate(uint32_t previous, uint32_t* out)
{
    for (uint32_t i = 0; i < g_ClusterCount; i++)
    {
        uint32_t cluster = 2 + (g_NextFree - 2 + i) % g_ClusterCount;
        uint32_t value;
        if (!Fat_Get(cluster, &value))
            return FAT_ERR_IO;
        if (value != 0)
            continue;

        if (!Fat_Set(cluster, Fat_EndOfChain()))
            return FAT_ERR_IO;
        if (previous != 0 && !Fat_Set(previous, cluster))
            return FAT_ERR_IO;

        g_FreeClusters--;
        g_NextFree = cluster + 1 < g_ClusterCount + 2 ? cluster + 1 : 2;
        *out = cluster;
        return FAT_OK;
    }
    return FAT_ERR_DISK_FULL;
}

static FAT_Status Chain_Free(uint32_t first)
{
    uint32_t cluster = first;
    for (uint32_t guard = 0; Cluster_IsValid(cluster) && guard < g_ClusterCount; guard++)
    {
        uint32_t next;
        if (!Fat_Get(cluster, &next))
            return FAT_ERR_IO;
        if (!Fat_Set(cluster, 0))
            return FAT_ERR_IO;
        g_FreeClusters++;
        if (Fat_IsEndOfChain(next))
            break;
        cluster = next;
    }
    return FAT_OK;
}

// writes the free-cluster count in the FAT32 FSInfo sector and flushes the disk
static void FAT_Sync()
{
    if (g_FatType == 32 && g_FSInfoSector != 0)
    {
        uint8_t* sector = Cache_Read(g_FSInfoSector);
        if (sector != NULL
            && *(uint32_t*)(sector + 0) == 0x41615252
            && *(uint32_t*)(sector + 484) == 0x61417272)
        {
            *(uint32_t*)(sector + 488) = g_FreeClusters;
            *(uint32_t*)(sector + 492) = g_NextFree;
            Cache_Write(g_FSInfoSector, sector);
        }
    }
    ATA_Flush();
}

// --- date and time ------------------------------------------------------------------

static void FAT_Now(uint16_t* date, uint16_t* time)
{
    DateTime dt;
    RTC_Read(&dt);
    int year = dt.Year < 1980 ? 1980 : dt.Year;
    *date = ((year - 1980) << 9) | (dt.Month << 5) | dt.Day;
    *time = (dt.Hour << 11) | (dt.Minute << 5) | (dt.Second / 2);
}

void FAT_DecodeDate(uint16_t date, int* year, int* month, int* day)
{
    *year = 1980 + (date >> 9);
    *month = (date >> 5) & 0x0F;
    *day = date & 0x1F;
}

void FAT_DecodeTime(uint16_t time, int* hour, int* minute, int* second)
{
    *hour = time >> 11;
    *minute = (time >> 5) & 0x3F;
    *second = (time & 0x1F) * 2;
}

// --- names -----------------------------------------------------------------------------

// The screen uses code page 437; long names are UTF-16. A few accented letters:
static const struct { uint8_t cp437; uint16_t unicode; } g_CharMap[] = {
    { 0x85, 0x00E0 }, { 0x8A, 0x00E8 }, { 0x82, 0x00E9 }, { 0x8D, 0x00EC },
    { 0x95, 0x00F2 }, { 0x97, 0x00F9 }, { 0x87, 0x00E7 }, { 0xF8, 0x00B0 },
    { 0x9C, 0x00A3 }, { 0x15, 0x00A7 }, { 0x81, 0x00FC }, { 0x84, 0x00E4 },
    { 0x94, 0x00F6 }, { 0x8E, 0x00C4 }, { 0x99, 0x00D6 }, { 0x9A, 0x00DC },
};

static uint8_t UnicodeToCP437(uint16_t c)
{
    if (c < 0x80)
        return (uint8_t)c;
    for (size_t i = 0; i < sizeof(g_CharMap) / sizeof(g_CharMap[0]); i++)
        if (g_CharMap[i].unicode == c)
            return g_CharMap[i].cp437;
    return '?';
}

static uint16_t CP437ToUnicode(uint8_t c)
{
    if (c < 0x80)
        return c;
    for (size_t i = 0; i < sizeof(g_CharMap) / sizeof(g_CharMap[0]); i++)
        if (g_CharMap[i].cp437 == c)
            return g_CharMap[i].unicode;
    return '_';
}

static uint8_t LFN_Checksum(const uint8_t name[11])
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
    return sum;
}

// "NOTE    TXT" → "NOTE.TXT" (or "note.txt" with the lowercase flags)
static void ShortName_Format(const DirEntry* raw, char* out, bool applyCase)
{
    int pos = 0;
    for (int i = 0; i < 8 && raw->Name[i] != ' '; i++)
    {
        char c = (i == 0 && raw->Name[0] == 0x05) ? (char)0xE5 : raw->Name[i];
        out[pos++] = (applyCase && (raw->NTReserved & NT_LOWERCASE_BASE)) ? tolower(c) : c;
    }
    if (raw->Name[8] != ' ')
    {
        out[pos++] = '.';
        for (int i = 8; i < 11 && raw->Name[i] != ' '; i++)
            out[pos++] = (applyCase && (raw->NTReserved & NT_LOWERCASE_EXT)) ? tolower(raw->Name[i]) : raw->Name[i];
    }
    out[pos] = '\0';
}

static bool IsValidShortChar(uint8_t c)
{
    return isalnum(c) || (c != 0 && strchr("$%'-_@~`!(){}^#&", c) != NULL);
}

static FAT_Status Name_Validate(const char* name)
{
    size_t len = strlen(name);
    if (len == 0 || len > FAT_MAX_NAME)
        return FAT_ERR_INVALID_NAME;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return FAT_ERR_INVALID_NAME;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t c = (uint8_t)name[i];
        if (c < 0x20 || strchr("\\/:*?\"<>|", c) != NULL)
            return FAT_ERR_INVALID_NAME;
    }
    if (name[len - 1] == ' ' || name[len - 1] == '.')
        return FAT_ERR_INVALID_NAME;
    return FAT_OK;
}

// If "name" is a valid 8.3 name (all lowercase or all uppercase in each part)
// fills the 11 bytes and the NT case flags, and returns true.
static bool Name_ToShort(const char* name, uint8_t out[11], uint8_t* ntFlags)
{
    const char* dot = strchr(name, '.');
    if (dot != NULL && strchr(dot + 1, '.') != NULL)
        return false;           // more than one dot
    if (dot == name)
        return false;           // ".hidden"

    size_t baseLen = dot ? (size_t)(dot - name) : strlen(name);
    size_t extLen = dot ? strlen(dot + 1) : 0;
    if (baseLen == 0 || baseLen > 8 || extLen > 3)
        return false;

    // check characters and case of each part
    bool baseLower = false, baseUpper = false, extLower = false, extUpper = false;
    for (size_t i = 0; i < baseLen; i++)
    {
        uint8_t c = name[i];
        if (!IsValidShortChar(c)) return false;
        if (c >= 'a' && c <= 'z') baseLower = true;
        if (c >= 'A' && c <= 'Z') baseUpper = true;
    }
    for (size_t i = 0; i < extLen; i++)
    {
        uint8_t c = dot[1 + i];
        if (!IsValidShortChar(c)) return false;
        if (c >= 'a' && c <= 'z') extLower = true;
        if (c >= 'A' && c <= 'Z') extUpper = true;
    }
    if ((baseLower && baseUpper) || (extLower && extUpper))
        return false;           // "Note.txt" needs a long name to keep its case

    memset(out, ' ', 11);
    for (size_t i = 0; i < baseLen; i++)
        out[i] = toupper(name[i]);
    for (size_t i = 0; i < extLen; i++)
        out[8 + i] = toupper(dot[1 + i]);

    *ntFlags = (baseLower ? NT_LOWERCASE_BASE : 0) | (extLower ? NT_LOWERCASE_EXT : 0);
    return true;
}

// --- directories ----------------------------------------------------------------------
// A directory is identified by its first cluster; 0 means the root directory.

typedef struct
{
    uint32_t Dir;
    uint32_t Index;             // current slot (entry) index
    uint32_t Cluster;           // cluster that contains the current slot
    uint32_t ClusterIndex;      // position of Cluster in the chain
} DirIter;

static bool Dir_IsFixedRoot(uint32_t dir)
{
    return dir == 0 && g_FatType != 32;
}

static uint32_t Dir_EntriesPerCluster()
{
    return g_SectorsPerCluster * ENTRIES_PER_SECTOR;
}

static void Iter_Init(DirIter* it, uint32_t dir)
{
    it->Dir = dir;
    it->Index = 0;
    it->ClusterIndex = 0;
    it->Cluster = Dir_IsFixedRoot(dir) ? 0 : (dir == 0 ? g_RootCluster : dir);
}

// Finds where slot it->Index is stored. Returns false past the end of the directory.
// (it->Index can only grow: to go back, call Iter_Init again)
static bool Iter_Locate(DirIter* it, uint32_t* lba, uint32_t* offset)
{
    *offset = (it->Index % ENTRIES_PER_SECTOR) * ENTRY_SIZE;

    if (Dir_IsFixedRoot(it->Dir))
    {
        if (it->Index >= g_RootEntryCount)
            return false;
        *lba = g_RootDirLba + it->Index / ENTRIES_PER_SECTOR;
        return true;
    }

    uint32_t wanted = it->Index / Dir_EntriesPerCluster();
    while (it->ClusterIndex < wanted)
    {
        uint32_t next;
        if (!Fat_Get(it->Cluster, &next) || !Cluster_IsValid(next))
            return false;
        it->Cluster = next;
        it->ClusterIndex++;
    }
    if (!Cluster_IsValid(it->Cluster))
        return false;

    *lba = Cluster_ToLba(it->Cluster) + (it->Index % Dir_EntriesPerCluster()) / ENTRIES_PER_SECTOR;
    return true;
}

static FAT_Status Dir_ReadSlot(uint32_t dir, uint32_t index, DirEntry* out)
{
    DirIter it;
    Iter_Init(&it, dir);
    it.Index = index;
    uint32_t lba, offset;
    if (!Iter_Locate(&it, &lba, &offset))
        return FAT_ERR_INVALID;
    uint8_t* sector = Cache_Read(lba);
    if (sector == NULL)
        return FAT_ERR_IO;
    memcpy(out, sector + offset, ENTRY_SIZE);
    return FAT_OK;
}

static FAT_Status Dir_WriteSlot(uint32_t dir, uint32_t index, const void* data)
{
    DirIter it;
    Iter_Init(&it, dir);
    it.Index = index;
    uint32_t lba, offset;
    if (!Iter_Locate(&it, &lba, &offset))
        return FAT_ERR_INVALID;
    uint8_t* sector = Cache_Read(lba);
    if (sector == NULL)
        return FAT_ERR_IO;
    memcpy(sector + offset, data, ENTRY_SIZE);
    return Cache_Write(lba, sector) ? FAT_OK : FAT_ERR_IO;
}

// adds a new zeroed cluster at the end of a (non fixed) directory
static FAT_Status Dir_Extend(DirIter* it)
{
    uint32_t cluster;
    FAT_Status status = Cluster_Allocate(it->Cluster, &cluster);
    if (status != FAT_OK)
        return status;
    return Cluster_Zero(cluster) ? FAT_OK : FAT_ERR_IO;
}

typedef bool (*EntryVisitor)(FAT_Entry* entry, const DirEntry* raw, void* context);

static void Entry_Fill(FAT_Entry* e, const DirEntry* raw, uint32_t dir, uint32_t index)
{
    ShortName_Format(raw, e->ShortName, false);
    e->IsDirectory = (raw->Attributes & ATTR_DIRECTORY) != 0;
    e->Size = raw->Size;
    e->FirstCluster = raw->FirstClusterLow | ((uint32_t)raw->FirstClusterHigh << 16);
    e->ModifiedDate = raw->ModifiedDate;
    e->ModifiedTime = raw->ModifiedTime;
    e->DirCluster = dir;
    e->SlotIndex = index;
    e->FirstSlot = index;
}

// Calls "visitor" for every entry of the directory, putting together the long names.
static FAT_Status Dir_Visit(uint32_t dir, EntryVisitor visitor, void* context, bool includeDots)
{
    DirIter it;
    Iter_Init(&it, dir);

    uint16_t lfn[LFN_MAX_PARTS * LFN_CHARS + 1];
    bool lfnValid = false;
    int lfnExpected = 0;
    uint8_t lfnChecksum = 0;
    uint32_t lfnFirst = 0;

    for (;; it.Index++)
    {
        uint32_t lba, offset;
        if (!Iter_Locate(&it, &lba, &offset))
            break;
        uint8_t* sector = Cache_Read(lba);
        if (sector == NULL)
            return FAT_ERR_IO;

        DirEntry raw;
        memcpy(&raw, sector + offset, ENTRY_SIZE);

        if (raw.Name[0] == 0x00)            // no more entries
            break;
        if (raw.Name[0] == DELETED_MARK)
        {
            lfnValid = false;
            continue;
        }

        if (raw.Attributes == ATTR_LFN)
        {
            const LFNEntry* l = (const LFNEntry*)&raw;
            int order = l->Order & 0x1F;

            if (l->Order & LFN_LAST)
            {
                // first entry on disk = last piece of the name
                if (order == 0 || order > LFN_MAX_PARTS)
                {
                    lfnValid = false;
                    continue;
                }
                lfnValid = true;
                lfnExpected = order;
                lfnChecksum = l->Checksum;
                lfnFirst = it.Index;
                memset(lfn, 0, sizeof(lfn));
            }

            if (!lfnValid || order != lfnExpected || l->Checksum != lfnChecksum)
            {
                lfnValid = false;
                continue;
            }

            uint16_t* dst = lfn + (order - 1) * LFN_CHARS;
            memcpy(dst, l->Name1, sizeof(l->Name1));
            memcpy(dst + 5, l->Name2, sizeof(l->Name2));
            memcpy(dst + 11, l->Name3, sizeof(l->Name3));
            lfnExpected--;
            continue;
        }

        if (raw.Attributes & ATTR_VOLUME_ID)    // volume label
        {
            lfnValid = false;
            continue;
        }

        FAT_Entry entry;
        Entry_Fill(&entry, &raw, dir, it.Index);

        bool haveLfn = lfnValid && lfnExpected == 0 && LFN_Checksum(raw.Name) == lfnChecksum;
        if (haveLfn)
        {
            int i = 0;
            for (; i < FAT_MAX_NAME && lfn[i] != 0 && lfn[i] != 0xFFFF; i++)
                entry.Name[i] = UnicodeToCP437(lfn[i]);
            entry.Name[i] = '\0';
            entry.FirstSlot = lfnFirst;
        }
        else
            ShortName_Format(&raw, entry.Name, true);
        lfnValid = false;

        if (!includeDots && (strcmp(entry.Name, ".") == 0 || strcmp(entry.Name, "..") == 0))
            continue;

        if (!visitor(&entry, &raw, context))
            return FAT_OK;
    }
    return FAT_OK;
}

typedef struct
{
    const char* Name;
    FAT_Entry* Result;
    DirEntry* Raw;
    bool Found;
} FindContext;

static bool FindVisitor(FAT_Entry* entry, const DirEntry* raw, void* context)
{
    FindContext* ctx = (FindContext*)context;
    if (strcasecmp(entry->Name, ctx->Name) == 0 || strcasecmp(entry->ShortName, ctx->Name) == 0)
    {
        *ctx->Result = *entry;
        if (ctx->Raw != NULL)
            *ctx->Raw = *raw;
        ctx->Found = true;
        return false;
    }
    return true;
}

static FAT_Status Dir_Find(uint32_t dir, const char* name, FAT_Entry* result, DirEntry* raw)
{
    FindContext ctx = { name, result, raw, false };
    FAT_Status status = Dir_Visit(dir, FindVisitor, &ctx, false);
    if (status != FAT_OK)
        return status;
    return ctx.Found ? FAT_OK : FAT_ERR_NOT_FOUND;
}

typedef struct
{
    const uint8_t* ShortName;
    bool Found;
} ShortNameContext;

static bool ShortNameVisitor(FAT_Entry* entry, const DirEntry* raw, void* context)
{
    ShortNameContext* ctx = (ShortNameContext*)context;
    if (memcmp(raw->Name, ctx->ShortName, 11) == 0)
    {
        ctx->Found = true;
        return false;
    }
    return true;
}

// Builds a unique 8.3 alias for a long name: "lista della spesa.txt" → "LISTAD~1.TXT"
static FAT_Status Name_MakeAlias(uint32_t dir, const char* name, uint8_t out[11])
{
    const char* dot = strrchr(name, '.');
    if (dot == name)
        dot = NULL;

    char base[9], ext[4];
    int baseLen = 0, extLen = 0;
    for (const char* p = name; *p && p != dot && baseLen < 8; p++)
    {
        uint8_t c = *p;
        if (c == ' ' || c == '.')
            continue;
        base[baseLen++] = IsValidShortChar(c) ? toupper(c) : '_';
    }
    if (dot != NULL)
        for (const char* p = dot + 1; *p && extLen < 3; p++)
        {
            uint8_t c = *p;
            if (c == ' ' || c == '.')
                continue;
            ext[extLen++] = IsValidShortChar(c) ? toupper(c) : '_';
        }
    if (baseLen == 0)
        base[baseLen++] = '_';

    for (uint32_t n = 1; n < 1000000; n++)
    {
        char tail[8];
        int tailLen = 0;
        char digits[8];
        int d = 0;
        for (uint32_t v = n; v > 0; v /= 10)
            digits[d++] = '0' + v % 10;
        tail[tailLen++] = '~';
        while (d > 0)
            tail[tailLen++] = digits[--d];

        int keep = baseLen < 8 - tailLen ? baseLen : 8 - tailLen;
        memset(out, ' ', 11);
        for (int i = 0; i < keep; i++)
            out[i] = base[i];
        for (int i = 0; i < tailLen; i++)
            out[keep + i] = tail[i];
        for (int i = 0; i < extLen; i++)
            out[8 + i] = ext[i];

        ShortNameContext ctx = { out, false };
        FAT_Status status = Dir_Visit(dir, ShortNameVisitor, &ctx, true);
        if (status != FAT_OK)
            return status;
        if (!ctx.Found)
            return FAT_OK;
    }
    return FAT_ERR_DIR_FULL;
}

// Creates the entries (long name + 8.3) for "name" in "dir". "templ" gives the
// attributes, cluster, size and dates to use.
static FAT_Status Dir_AddEntry(uint32_t dir, const char* name, const DirEntry* templ, FAT_Entry* result)
{
    FAT_Status status = Name_Validate(name);
    if (status != FAT_OK)
        return status;

    FAT_Entry existing;
    status = Dir_Find(dir, name, &existing, NULL);
    if (status == FAT_OK)
        return FAT_ERR_EXISTS;
    if (status != FAT_ERR_NOT_FOUND)
        return status;

    DirEntry entry = *templ;
    uint8_t ntFlags = 0;
    int lfnCount = 0;
    if (Name_ToShort(name, entry.Name, &ntFlags))
        entry.NTReserved = ntFlags;
    else
    {
        status = Name_MakeAlias(dir, name, entry.Name);
        if (status != FAT_OK)
            return status;
        entry.NTReserved = 0;
        lfnCount = (strlen(name) + LFN_CHARS - 1) / LFN_CHARS;
    }

    // find (or make room for) lfnCount + 1 consecutive free slots
    uint32_t needed = lfnCount + 1;
    uint32_t runStart = 0, runLength = 0;
    DirIter it;
    Iter_Init(&it, dir);
    for (;; it.Index++)
    {
        uint32_t lba, offset;
        if (!Iter_Locate(&it, &lba, &offset))
        {
            if (Dir_IsFixedRoot(dir))
                return FAT_ERR_DIR_FULL;
            status = Dir_Extend(&it);
            if (status != FAT_OK)
                return status;
            if (!Iter_Locate(&it, &lba, &offset))
                return FAT_ERR_IO;
        }
        uint8_t* sector = Cache_Read(lba);
        if (sector == NULL)
            return FAT_ERR_IO;

        uint8_t first = sector[offset];
        if (first == 0x00 || first == DELETED_MARK)
        {
            if (runLength == 0)
                runStart = it.Index;
            if (++runLength == needed)
                break;
        }
        else
            runLength = 0;
    }

    // write the long-name entries (last piece first), then the 8.3 entry
    uint8_t checksum = LFN_Checksum(entry.Name);
    size_t nameLen = strlen(name);
    for (int part = lfnCount; part >= 1; part--)
    {
        LFNEntry l;
        memset(&l, 0, sizeof(l));
        l.Order = part | (part == lfnCount ? LFN_LAST : 0);
        l.Attributes = ATTR_LFN;
        l.Checksum = checksum;

        uint16_t chars[LFN_CHARS];
        for (int i = 0; i < LFN_CHARS; i++)
        {
            size_t pos = (part - 1) * LFN_CHARS + i;
            if (pos < nameLen)
                chars[i] = CP437ToUnicode((uint8_t)name[pos]);
            else if (pos == nameLen)
                chars[i] = 0x0000;      // terminator
            else
                chars[i] = 0xFFFF;      // padding
        }
        memcpy(l.Name1, chars, sizeof(l.Name1));
        memcpy(l.Name2, chars + 5, sizeof(l.Name2));
        memcpy(l.Name3, chars + 11, sizeof(l.Name3));

        status = Dir_WriteSlot(dir, runStart + (lfnCount - part), &l);
        if (status != FAT_OK)
            return status;
    }

    uint32_t index = runStart + lfnCount;
    status = Dir_WriteSlot(dir, index, &entry);
    if (status != FAT_OK)
        return status;

    if (result != NULL)
    {
        Entry_Fill(result, &entry, dir, index);
        strlcpy(result->Name, name, sizeof(result->Name));
        result->FirstSlot = runStart;
    }
    return FAT_OK;
}

// marks the entry (and its long-name entries) as deleted
static FAT_Status Dir_DeleteEntry(const FAT_Entry* entry)
{
    for (uint32_t i = entry->FirstSlot; i <= entry->SlotIndex; i++)
    {
        DirEntry raw;
        FAT_Status status = Dir_ReadSlot(entry->DirCluster, i, &raw);
        if (status != FAT_OK)
            return status;
        raw.Name[0] = DELETED_MARK;
        status = Dir_WriteSlot(entry->DirCluster, i, &raw);
        if (status != FAT_OK)
            return status;
    }
    return FAT_OK;
}

static void DirEntry_SetCluster(DirEntry* raw, uint32_t cluster)
{
    raw->FirstClusterLow = cluster & 0xFFFF;
    raw->FirstClusterHigh = (cluster >> 16) & 0xFFFF;
}

static void DirEntry_Init(DirEntry* raw, uint8_t attributes, uint32_t cluster, uint32_t size)
{
    memset(raw, 0, sizeof(*raw));
    memset(raw->Name, ' ', 11);
    raw->Attributes = attributes;
    uint16_t date, time;
    FAT_Now(&date, &time);
    raw->CreatedDate = raw->ModifiedDate = raw->AccessedDate = date;
    raw->CreatedTime = raw->ModifiedTime = time;
    DirEntry_SetCluster(raw, cluster);
    raw->Size = size;
}

// --- paths --------------------------------------------------------------------------------

static void Entry_MakeRoot(FAT_Entry* entry)
{
    memset(entry, 0, sizeof(*entry));
    strlcpy(entry->Name, "/", sizeof(entry->Name));
    strlcpy(entry->ShortName, "/", sizeof(entry->ShortName));
    entry->IsDirectory = true;
}

static FAT_Status Lookup(const char* path, FAT_Entry* entry, DirEntry* raw)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (path[0] != '/')
        return FAT_ERR_INVALID;

    Entry_MakeRoot(entry);
    if (raw != NULL)
        memset(raw, 0, sizeof(*raw));

    uint32_t dir = 0;
    const char* p = path;
    while (*p)
    {
        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        char name[FAT_MAX_NAME + 1];
        size_t len = 0;
        while (*p && *p != '/')
        {
            if (len < FAT_MAX_NAME)
                name[len++] = *p;
            p++;
        }
        name[len] = '\0';

        if (!entry->IsDirectory)
            return FAT_ERR_NOT_DIR;

        FAT_Status status = Dir_Find(dir, name, entry, raw);
        if (status != FAT_OK)
            return status;
        dir = entry->FirstCluster;
    }
    return FAT_OK;
}

// finds the directory that should contain "path" and the name of the last component
static FAT_Status LookupParent(const char* path, uint32_t* dir, char* name)
{
    char parent[PATH_MAX_LENGTH];
    Path_Split(path, parent, sizeof(parent), name, FAT_MAX_NAME + 1);

    FAT_Entry entry;
    FAT_Status status = Lookup(parent, &entry, NULL);
    if (status != FAT_OK)
        return status;
    if (!entry.IsDirectory)
        return FAT_ERR_NOT_DIR;
    *dir = entry.FirstCluster;
    return FAT_OK;
}

static bool IsProtected(const char* path)
{
    return Path_IsInside(path, PROTECTED_PATH);
}

// --- public functions ------------------------------------------------------------------------

bool FAT_Mount()
{
    g_Mounted = false;
    g_PartitionLba = 0;
    Cache_Clear();

    if (!ATA_IsPresent() && !ATA_Initialize())
    {
        log_warn(MODULE, "no ATA disk");
        return false;
    }

    // sector 0: MBR (partition table) or directly the volume boot sector
    uint8_t* sector = Cache_Read(0);
    if (sector == NULL || sector[510] != 0x55 || sector[511] != 0xAA)
    {
        log_warn(MODULE, "disk has no boot signature");
        return false;
    }

    const BootSector* bs = (const BootSector*)sector;
    bool isVolume = (bs->Jump[0] == 0xEB || bs->Jump[0] == 0xE9) && bs->BytesPerSector == SECTOR_SIZE
                    && bs->SectorsPerCluster != 0 && bs->FatCount != 0;
    if (!isVolume)
    {
        // look for a FAT partition, preferring the active one
        uint32_t found = 0;
        for (int i = 0; i < 4; i++)
        {
            const uint8_t* p = sector + 446 + i * 16;
            uint8_t type = p[4];
            uint32_t start = *(const uint32_t*)(p + 8);
            bool isFat = type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0B
                         || type == 0x0C || type == 0x0E;
            if (isFat && start != 0 && (found == 0 || (p[0] & 0x80)))
                found = start;
        }
        if (found == 0)
        {
            log_warn(MODULE, "no FAT partition found");
            return false;
        }
        g_PartitionLba = found;
        Cache_Clear();
        sector = Cache_Read(0);
        if (sector == NULL)
            return false;
        bs = (const BootSector*)sector;
    }

    if (bs->BytesPerSector != SECTOR_SIZE || bs->SectorsPerCluster == 0 || bs->FatCount == 0
        || bs->ReservedSectors == 0)
    {
        log_warn(MODULE, "unsupported boot sector");
        return false;
    }

    uint32_t totalSectors = bs->TotalSectors16 ? bs->TotalSectors16 : bs->TotalSectors32;
    g_SectorsPerCluster = bs->SectorsPerCluster;
    g_ClusterSize = g_SectorsPerCluster * SECTOR_SIZE;
    g_ReservedSectors = bs->ReservedSectors;
    g_FatCount = bs->FatCount;
    g_SectorsPerFat = bs->SectorsPerFat16 ? bs->SectorsPerFat16 : bs->Fat32.SectorsPerFat;
    g_RootEntryCount = bs->RootEntryCount;

    uint32_t rootDirSectors = (g_RootEntryCount * ENTRY_SIZE + SECTOR_SIZE - 1) / SECTOR_SIZE;
    g_RootDirLba = g_ReservedSectors + g_FatCount * g_SectorsPerFat;
    g_DataLba = g_RootDirLba + rootDirSectors;
    g_ClusterCount = (totalSectors - g_DataLba) / g_SectorsPerCluster;

    const uint8_t* label;
    if (bs->SectorsPerFat16 == 0)
    {
        g_FatType = 32;
        g_RootCluster = bs->Fat32.RootCluster;
        g_FSInfoSector = bs->Fat32.FSInfoSector;
        if (g_FSInfoSector == 0 || g_FSInfoSector == 0xFFFF || g_FSInfoSector >= g_ReservedSectors)
            g_FSInfoSector = 0;
        label = bs->Fat32.VolumeLabel;
    }
    else
    {
        g_FatType = g_ClusterCount < 4085 ? 12 : 16;
        g_RootCluster = 0;
        g_FSInfoSector = 0;
        label = bs->Fat16.VolumeLabel;
    }

    memcpy(g_VolumeLabel, label, 11);
    g_VolumeLabel[11] = '\0';
    for (int i = 10; i >= 0 && g_VolumeLabel[i] == ' '; i--)
        g_VolumeLabel[i] = '\0';

    // count the free clusters (to show the free space)
    g_FreeClusters = 0;
    for (uint32_t c = 2; c < g_ClusterCount + 2; c++)
    {
        uint32_t value;
        if (!Fat_Get(c, &value))
            return false;
        if (value == 0)
            g_FreeClusters++;
    }
    g_NextFree = 2;

    g_Mounted = true;
    log_info(MODULE, "mounted FAT%d at sector %u: %u clusters of %u bytes, %u free",
             g_FatType, g_PartitionLba, g_ClusterCount, g_ClusterSize, g_FreeClusters);
    return true;
}

bool FAT_IsMounted()                { return g_Mounted; }
int FAT_GetType()                   { return g_FatType; }
uint32_t FAT_GetClusterSize()       { return g_ClusterSize; }
const char* FAT_GetVolumeLabel()    { return g_VolumeLabel; }
uint64_t FAT_GetTotalBytes()        { return (uint64_t)g_ClusterCount * g_ClusterSize; }
uint64_t FAT_GetFreeBytes()         { return (uint64_t)g_FreeClusters * g_ClusterSize; }

const char* FAT_StatusString(FAT_Status status)
{
    switch (status)
    {
        case FAT_OK:                return "ok";
        case FAT_ERR_NOT_MOUNTED:   return "nessun disco (file system non montato)";
        case FAT_ERR_IO:            return "errore di lettura/scrittura del disco";
        case FAT_ERR_NOT_FOUND:     return "file o cartella inesistente";
        case FAT_ERR_EXISTS:        return "esiste gia'";
        case FAT_ERR_NOT_DIR:       return "non e' una cartella";
        case FAT_ERR_IS_DIR:        return "e' una cartella";
        case FAT_ERR_NOT_EMPTY:     return "la cartella non e' vuota";
        case FAT_ERR_DISK_FULL:     return "disco pieno";
        case FAT_ERR_DIR_FULL:      return "cartella piena";
        case FAT_ERR_INVALID_NAME:  return "nome non valido";
        case FAT_ERR_TOO_BIG:       return "file troppo grande";
        case FAT_ERR_PROTECTED:     return "operazione non permessa su /boot (contiene il kernel)";
        case FAT_ERR_INVALID:       return "operazione non valida";
    }
    return "errore sconosciuto";
}

FAT_Status FAT_Lookup(const char* path, FAT_Entry* entry)
{
    return Lookup(path, entry, NULL);
}

typedef struct
{
    FAT_DirCallback Callback;
    void* Context;
} ListContext;

static bool ListVisitor(FAT_Entry* entry, const DirEntry* raw, void* context)
{
    ListContext* ctx = (ListContext*)context;
    return ctx->Callback(entry, ctx->Context);
}

FAT_Status FAT_ListDirectory(const char* path, FAT_DirCallback callback, void* context)
{
    FAT_Entry dir;
    FAT_Status status = Lookup(path, &dir, NULL);
    if (status != FAT_OK)
        return status;
    if (!dir.IsDirectory)
        return FAT_ERR_NOT_DIR;

    ListContext ctx = { callback, context };
    return Dir_Visit(dir.FirstCluster, ListVisitor, &ctx, false);
}

FAT_Status FAT_ReadStream(const char* path, FAT_ReadCallback callback, void* context)
{
    FAT_Entry entry;
    FAT_Status status = Lookup(path, &entry, NULL);
    if (status != FAT_OK)
        return status;
    if (entry.IsDirectory)
        return FAT_ERR_IS_DIR;

    uint32_t remaining = entry.Size;
    uint32_t cluster = entry.FirstCluster;
    for (uint32_t guard = 0; remaining > 0 && guard <= g_ClusterCount; guard++)
    {
        if (!Cluster_IsValid(cluster))
            return FAT_ERR_IO;          // chain shorter than the file size

        for (uint32_t s = 0; s < g_SectorsPerCluster && remaining > 0; s++)
        {
            uint8_t* sector = Cache_Read(Cluster_ToLba(cluster) + s);
            if (sector == NULL)
                return FAT_ERR_IO;
            uint32_t take = remaining < SECTOR_SIZE ? remaining : SECTOR_SIZE;

            uint8_t copy[SECTOR_SIZE];  // the callback may use the cache
            memcpy(copy, sector, take);
            callback(copy, take, context);
            remaining -= take;
        }

        if (remaining > 0 && !Fat_Get(cluster, &cluster))
            return FAT_ERR_IO;
    }
    return FAT_OK;
}

typedef struct
{
    uint8_t* Buffer;
    uint32_t Length;
} ReadContext;

static void ReadToBuffer(const uint8_t* data, uint32_t length, void* context)
{
    ReadContext* ctx = (ReadContext*)context;
    memcpy(ctx->Buffer + ctx->Length, data, length);
    ctx->Length += length;
}

FAT_Status FAT_ReadFile(const char* path, void* buffer, uint32_t capacity, uint32_t* size)
{
    FAT_Entry entry;
    FAT_Status status = Lookup(path, &entry, NULL);
    if (status != FAT_OK)
        return status;
    if (entry.IsDirectory)
        return FAT_ERR_IS_DIR;
    if (entry.Size > capacity)
        return FAT_ERR_TOO_BIG;

    ReadContext ctx = { (uint8_t*)buffer, 0 };
    status = FAT_ReadStream(path, ReadToBuffer, &ctx);
    *size = ctx.Length;
    return status;
}

// allocates a new chain and writes "data" into it
static FAT_Status WriteChain(const void* data, uint32_t size, uint32_t* firstCluster)
{
    *firstCluster = 0;
    const uint8_t* in = (const uint8_t*)data;
    uint32_t written = 0, previous = 0;

    while (written < size)
    {
        uint32_t cluster;
        FAT_Status status = Cluster_Allocate(previous, &cluster);
        if (status != FAT_OK)
        {
            if (*firstCluster != 0)
                Chain_Free(*firstCluster);
            *firstCluster = 0;
            return status;
        }
        if (*firstCluster == 0)
            *firstCluster = cluster;

        for (uint32_t s = 0; s < g_SectorsPerCluster; s++)
        {
            uint8_t sector[SECTOR_SIZE];
            memset(sector, 0, sizeof(sector));
            if (written < size)
            {
                uint32_t take = size - written < SECTOR_SIZE ? size - written : SECTOR_SIZE;
                memcpy(sector, in + written, take);
                written += take;
            }
            if (!Cache_Write(Cluster_ToLba(cluster) + s, sector))
            {
                Chain_Free(*firstCluster);
                *firstCluster = 0;
                return FAT_ERR_IO;
            }
        }
        previous = cluster;
    }
    return FAT_OK;
}

FAT_Status FAT_WriteFile(const char* path, const void* data, uint32_t size)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (IsProtected(path))
        return FAT_ERR_PROTECTED;

    uint32_t dir;
    char name[FAT_MAX_NAME + 1];
    FAT_Status status = LookupParent(path, &dir, name);
    if (status != FAT_OK)
        return status;

    FAT_Entry entry;
    DirEntry raw;
    status = Dir_Find(dir, name, &entry, &raw);
    if (status != FAT_OK && status != FAT_ERR_NOT_FOUND)
        return status;
    bool exists = (status == FAT_OK);
    if (exists && entry.IsDirectory)
        return FAT_ERR_IS_DIR;
    if (!exists)
    {
        status = Name_Validate(name);
        if (status != FAT_OK)
            return status;
    }

    // first write the new data, then update the entry, then free the old data:
    // if something fails halfway the old file is still there
    uint32_t firstCluster;
    status = WriteChain(data, size, &firstCluster);
    if (status != FAT_OK)
        return status;

    if (exists)
    {
        uint32_t oldCluster = entry.FirstCluster;
        DirEntry_SetCluster(&raw, firstCluster);
        raw.Size = size;
        raw.Attributes |= ATTR_ARCHIVE;
        {
            uint16_t date, time;
            FAT_Now(&date, &time);
            raw.ModifiedDate = date;
            raw.ModifiedTime = time;
        }
        raw.AccessedDate = raw.ModifiedDate;
        status = Dir_WriteSlot(dir, entry.SlotIndex, &raw);
        if (status != FAT_OK)
        {
            Chain_Free(firstCluster);
            return status;
        }
        if (oldCluster != 0)
            Chain_Free(oldCluster);
    }
    else
    {
        DirEntry templ;
        DirEntry_Init(&templ, ATTR_ARCHIVE, firstCluster, size);
        status = Dir_AddEntry(dir, name, &templ, NULL);
        if (status != FAT_OK)
        {
            if (firstCluster != 0)
                Chain_Free(firstCluster);
            return status;
        }
    }

    FAT_Sync();
    return FAT_OK;
}

FAT_Status FAT_Touch(const char* path)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (IsProtected(path))
        return FAT_ERR_PROTECTED;

    uint32_t dir;
    char name[FAT_MAX_NAME + 1];
    FAT_Status status = LookupParent(path, &dir, name);
    if (status != FAT_OK)
        return status;

    FAT_Entry entry;
    DirEntry raw;
    status = Dir_Find(dir, name, &entry, &raw);
    if (status == FAT_OK)
    {
        // already exists: only update the time
        {
            uint16_t date, time;
            FAT_Now(&date, &time);
            raw.ModifiedDate = date;
            raw.ModifiedTime = time;
        }
        raw.AccessedDate = raw.ModifiedDate;
        status = Dir_WriteSlot(dir, entry.SlotIndex, &raw);
    }
    else if (status == FAT_ERR_NOT_FOUND)
    {
        DirEntry templ;
        DirEntry_Init(&templ, ATTR_ARCHIVE, 0, 0);
        status = Dir_AddEntry(dir, name, &templ, NULL);
    }

    if (status == FAT_OK)
        FAT_Sync();
    return status;
}

FAT_Status FAT_MakeDirectory(const char* path)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (IsProtected(path))
        return FAT_ERR_PROTECTED;

    uint32_t parent;
    char name[FAT_MAX_NAME + 1];
    FAT_Status status = LookupParent(path, &parent, name);
    if (status != FAT_OK)
        return status;

    status = Name_Validate(name);
    if (status != FAT_OK)
        return status;

    FAT_Entry existing;
    status = Dir_Find(parent, name, &existing, NULL);
    if (status == FAT_OK)
        return FAT_ERR_EXISTS;
    if (status != FAT_ERR_NOT_FOUND)
        return status;

    // a new directory = one empty cluster with the "." and ".." entries
    uint32_t cluster;
    status = Cluster_Allocate(0, &cluster);
    if (status != FAT_OK)
        return status;
    if (!Cluster_Zero(cluster))
    {
        Chain_Free(cluster);
        return FAT_ERR_IO;
    }

    DirEntry dot, dotdot;
    DirEntry_Init(&dot, ATTR_DIRECTORY, cluster, 0);
    dot.Name[0] = '.';
    DirEntry_Init(&dotdot, ATTR_DIRECTORY, parent, 0);    // 0 = root, also on FAT32
    dotdot.Name[0] = dotdot.Name[1] = '.';

    uint8_t* sector = Cache_Read(Cluster_ToLba(cluster));
    if (sector == NULL)
    {
        Chain_Free(cluster);
        return FAT_ERR_IO;
    }
    memcpy(sector, &dot, ENTRY_SIZE);
    memcpy(sector + ENTRY_SIZE, &dotdot, ENTRY_SIZE);
    if (!Cache_Write(Cluster_ToLba(cluster), sector))
    {
        Chain_Free(cluster);
        return FAT_ERR_IO;
    }

    DirEntry templ;
    DirEntry_Init(&templ, ATTR_DIRECTORY, cluster, 0);
    status = Dir_AddEntry(parent, name, &templ, NULL);
    if (status != FAT_OK)
    {
        Chain_Free(cluster);
        return status;
    }

    FAT_Sync();
    return FAT_OK;
}

static bool EmptyVisitor(FAT_Entry* entry, const DirEntry* raw, void* context)
{
    *(bool*)context = false;
    return false;
}

FAT_Status FAT_Remove(const char* path)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (strcmp(path, "/") == 0)
        return FAT_ERR_INVALID;
    if (IsProtected(path))
        return FAT_ERR_PROTECTED;

    FAT_Entry entry;
    FAT_Status status = Lookup(path, &entry, NULL);
    if (status != FAT_OK)
        return status;

    if (entry.IsDirectory)
    {
        bool empty = true;
        status = Dir_Visit(entry.FirstCluster, EmptyVisitor, &empty, false);
        if (status != FAT_OK)
            return status;
        if (!empty)
            return FAT_ERR_NOT_EMPTY;
    }

    status = Dir_DeleteEntry(&entry);
    if (status != FAT_OK)
        return status;
    if (entry.FirstCluster != 0)
        status = Chain_Free(entry.FirstCluster);

    FAT_Sync();
    return status;
}

FAT_Status FAT_Rename(const char* from, const char* to)
{
    if (!g_Mounted)
        return FAT_ERR_NOT_MOUNTED;
    if (strcmp(from, "/") == 0)
        return FAT_ERR_INVALID;
    if (IsProtected(from) || IsProtected(to))
        return FAT_ERR_PROTECTED;
    if (strcmp(from, to) == 0)
        return FAT_OK;

    FAT_Entry source;
    DirEntry sourceRaw;
    FAT_Status status = Lookup(from, &source, &sourceRaw);
    if (status != FAT_OK)
        return status;

    // a directory can't go inside itself
    if (source.IsDirectory && Path_IsInside(to, from))
        return FAT_ERR_INVALID;

    uint32_t newParent;
    char newName[FAT_MAX_NAME + 1];
    status = LookupParent(to, &newParent, newName);
    if (status != FAT_OK)
        return status;

    bool caseOnly = strcasecmp(from, to) == 0;     // "note.txt" → "Note.txt"

    FAT_Entry target;
    status = Dir_Find(newParent, newName, &target, NULL);
    if (status == FAT_OK && !caseOnly)
    {
        // like "mv": an existing file is replaced, a directory is not
        if (target.IsDirectory || source.IsDirectory)
            return FAT_ERR_EXISTS;
        status = FAT_Remove(to);
        if (status != FAT_OK)
            return status;
        // the removal can't touch the source entry: they are different files
    }
    else if (status != FAT_OK && status != FAT_ERR_NOT_FOUND)
        return status;

    if (caseOnly)
    {
        // delete first, otherwise the new name "already exists"
        status = Dir_DeleteEntry(&source);
        if (status != FAT_OK)
            return status;
        status = Dir_AddEntry(newParent, newName, &sourceRaw, NULL);
    }
    else
    {
        status = Dir_AddEntry(newParent, newName, &sourceRaw, NULL);
        if (status != FAT_OK)
            return status;
        status = Dir_DeleteEntry(&source);
    }
    if (status != FAT_OK)
        return status;

    // a moved directory must point ".." to its new parent
    if (source.IsDirectory && source.DirCluster != newParent && source.FirstCluster != 0)
    {
        DirEntry dotdot;
        status = Dir_ReadSlot(source.FirstCluster, 1, &dotdot);
        if (status == FAT_OK && dotdot.Name[0] == '.' && dotdot.Name[1] == '.')
        {
            DirEntry_SetCluster(&dotdot, newParent);
            status = Dir_WriteSlot(source.FirstCluster, 1, &dotdot);
        }
    }

    FAT_Sync();
    return status;
}
