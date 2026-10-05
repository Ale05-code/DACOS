#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// FAT12/16/32 file system driver with read AND write support, used by the shell.
// It works on the first ATA disk: it reads the MBR, finds the FAT partition and
// "mounts" it. Every change is written to the disk immediately (write-through).
//
// All paths are absolute and normalized ("/", "/docs/note.txt"): use Path_Resolve
// (fs/path.h) to build them from what the user types.

#define FAT_MAX_NAME        64          // longest file name we accept (long names, LFN)

typedef enum {
    FAT_OK = 0,
    FAT_ERR_NOT_MOUNTED,
    FAT_ERR_IO,
    FAT_ERR_NOT_FOUND,
    FAT_ERR_EXISTS,
    FAT_ERR_NOT_DIR,
    FAT_ERR_IS_DIR,
    FAT_ERR_NOT_EMPTY,
    FAT_ERR_DISK_FULL,
    FAT_ERR_DIR_FULL,
    FAT_ERR_INVALID_NAME,
    FAT_ERR_TOO_BIG,
    FAT_ERR_PROTECTED,
    FAT_ERR_INVALID,
} FAT_Status;

typedef struct {
    char Name[FAT_MAX_NAME + 1];        // name to show (long name, or 8.3 with the right case)
    char ShortName[13];                 // 8.3 name, e.g. "NOTE.TXT"
    bool IsDirectory;
    uint32_t Size;
    uint32_t FirstCluster;              // 0 = empty file (or the root directory)
    uint16_t ModifiedDate, ModifiedTime;    // FAT format (see FAT_DecodeDate/Time)

    // where the entry is stored: directory, index of the 8.3 entry, index of
    // the first long-name entry (== SlotIndex if there is no long name)
    uint32_t DirCluster;
    uint32_t SlotIndex;
    uint32_t FirstSlot;
} FAT_Entry;

// called for every entry of a directory; return false to stop
typedef bool (*FAT_DirCallback)(const FAT_Entry* entry, void* context);

// called with consecutive pieces of a file
typedef void (*FAT_ReadCallback)(const uint8_t* data, uint32_t length, void* context);

bool FAT_Mount();
bool FAT_IsMounted();
const char* FAT_StatusString(FAT_Status status);

// information about the mounted volume
int FAT_GetType();                      // 12, 16 or 32
uint64_t FAT_GetTotalBytes();
uint64_t FAT_GetFreeBytes();
uint32_t FAT_GetClusterSize();
const char* FAT_GetVolumeLabel();

FAT_Status FAT_Lookup(const char* path, FAT_Entry* entry);
FAT_Status FAT_ListDirectory(const char* path, FAT_DirCallback callback, void* context);

FAT_Status FAT_ReadStream(const char* path, FAT_ReadCallback callback, void* context);
FAT_Status FAT_ReadFile(const char* path, void* buffer, uint32_t capacity, uint32_t* size);

// creates the file if it doesn't exist, otherwise replaces its content
FAT_Status FAT_WriteFile(const char* path, const void* data, uint32_t size);
// creates an empty file; if it already exists only updates its modification time
FAT_Status FAT_Touch(const char* path);
FAT_Status FAT_MakeDirectory(const char* path);
// removes a file or an EMPTY directory
FAT_Status FAT_Remove(const char* path);
// renames and/or moves a file or a directory ("to" is the complete new path)
FAT_Status FAT_Rename(const char* from, const char* to);

// date/time in FAT format → numbers
void FAT_DecodeDate(uint16_t date, int* year, int* month, int* day);
void FAT_DecodeTime(uint16_t time, int* hour, int* minute, int* second);
