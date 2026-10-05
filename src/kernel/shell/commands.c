#include "shell.h"
#include "shell_internal.h"
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <arch/i686/vga_text.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/rtc.h>
#include <arch/i686/pit.h>
#include <arch/i686/power.h>
#include <arch/i686/ata.h>

// --- small helpers -------------------------------------------------------------------

static void PrintStatusError(const char* command, const char* arg, FAT_Status status)
{
    Shell_Error(command, arg, FAT_StatusString(status));
}

static const char* BaseName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// "a" + "b" → "a/b" (no double slash for the root)
static bool JoinPath(char* out, const char* dir, const char* name)
{
    strlcpy(out, dir, PATH_MAX_LENGTH);
    if (strcmp(dir, "/") != 0 && strlcat(out, "/", PATH_MAX_LENGTH) >= PATH_MAX_LENGTH)
        return false;
    return strlcat(out, name, PATH_MAX_LENGTH) < PATH_MAX_LENGTH;
}

static void PrintSize(uint64_t bytes)
{
    if (bytes >= 10ull * 1024 * 1024)
        printf("%llu MB", bytes / (1024 * 1024));
    else if (bytes >= 10 * 1024)
        printf("%llu KB", bytes / 1024);
    else
        printf("%llu B", bytes);
}

static void PrintColoredName(const FAT_Entry* entry, int width)
{
    uint8_t color = VGA_GetColor();
    if (entry->IsDirectory)
        VGA_SetColor(VGA_ATTR(VGA_LIGHT_BLUE, VGA_BLACK));
    printf("%s", entry->Name);
    VGA_SetColor(color);
    for (int i = strlen(entry->Name); i < width; i++)
        putc(' ');
}

// --- help -------------------------------------------------------------------------------

static int Cmd_Help(int argc, char** argv)
{
    if (argc > 1)
    {
        const Command* command = Shell_FindCommand(argv[1]);
        if (command == NULL)
        {
            Shell_Error("help", argv[1], "comando non trovato");
            return 1;
        }
        printf("uso: %s\n%s\n", command->Usage, command->Description);
        return 0;
    }

    // compact list by category (the full descriptions with "help comando")
    static const char* const categories[][2] = {
        { "File e cartelle", "ls cd pwd mkdir rmdir touch create cat type" },
        { "",                "edit nano rm delete mv rename cp tree echo" },
        { "Sistema",         "neofetch date uptime free df uname whoami" },
        { "Shell",           "help clear history keymap" },
        { "Accensione",      "reboot shutdown" },
    };

    printf("Comandi di DACOS:\n\n");
    for (unsigned i = 0; i < sizeof(categories) / sizeof(categories[0]); i++)
    {
        VGA_SetColor(VGA_ATTR(VGA_YELLOW, VGA_BLACK));
        printf("  %-17s", categories[i][0]);
        VGA_SetColor(VGA_ATTR(VGA_LIGHT_GREEN, VGA_BLACK));
        printf("%s\n", categories[i][1]);
        VGA_SetColor(VGA_DEFAULT_COLOR);
    }
    printf("\nScrivi 'help comando' per sapere cosa fa e come si usa (es. help ls).\n");
    printf("Tasti: frecce = modifica e cronologia, Tab = completa, Ctrl+C = annulla,\n");
    printf("       Ctrl+L = pulisci lo schermo\n");
    printf("Redirezione: comando > file (sovrascrive), comando >> file (aggiunge in fondo)\n");
    return 0;
}

static int Cmd_Clear(int argc, char** argv)
{
    VGA_clrscr();
    return 0;
}

// --- directories ---------------------------------------------------------------------------

static int Cmd_Pwd(int argc, char** argv)
{
    printf("%s\n", g_Cwd);
    return 0;
}

static int Cmd_Cd(int argc, char** argv)
{
    char path[PATH_MAX_LENGTH];
    if (!Shell_ResolvePath("cd", argc > 1 ? argv[1] : "/", path))
        return 1;

    FAT_Entry entry;
    FAT_Status status = FAT_Lookup(path, &entry);
    if (status != FAT_OK)
    {
        PrintStatusError("cd", argv[1], status);
        return 1;
    }
    if (!entry.IsDirectory)
    {
        PrintStatusError("cd", argv[1], FAT_ERR_NOT_DIR);
        return 1;
    }
    strlcpy(g_Cwd, path, sizeof(g_Cwd));
    return 0;
}

// ls: entries are collected, sorted by name and printed in columns like on Linux
#define LS_MAX_ENTRIES  256

static FAT_Entry g_LsEntries[LS_MAX_ENTRIES];

typedef struct
{
    int Count;
    bool ShowAll;
    bool Truncated;
} LsContext;

static bool LsVisitor(const FAT_Entry* entry, void* context)
{
    LsContext* ctx = (LsContext*)context;
    if (!ctx->ShowAll && entry->Name[0] == '.')
        return true;
    if (ctx->Count >= LS_MAX_ENTRIES)
    {
        ctx->Truncated = true;
        return false;
    }
    g_LsEntries[ctx->Count++] = *entry;
    return true;
}

static void Ls_Sort(int count)
{
    for (int i = 1; i < count; i++)
    {
        FAT_Entry key = g_LsEntries[i];
        int j = i - 1;
        while (j >= 0 && strcasecmp(g_LsEntries[j].Name, key.Name) > 0)
        {
            g_LsEntries[j + 1] = g_LsEntries[j];
            j--;
        }
        g_LsEntries[j + 1] = key;
    }
}

static void Ls_PrintLong(const FAT_Entry* entry)
{
    int year, month, day, hour, minute, second;
    FAT_DecodeDate(entry->ModifiedDate, &year, &month, &day);
    FAT_DecodeTime(entry->ModifiedTime, &hour, &minute, &second);
    printf("%c %8u  %04d-%02d-%02d %02d:%02d  ", entry->IsDirectory ? 'd' : '-',
           entry->IsDirectory ? 0 : entry->Size, year, month, day, hour, minute);
    PrintColoredName(entry, 0);
    printf("\n");
}

static void Ls_PrintColumns(int count)
{
    int width = 1;
    for (int i = 0; i < count; i++)
    {
        int len = strlen(g_LsEntries[i].Name);
        if (len > width)
            width = len;
    }
    width += 2;
    int columns = VGA_WIDTH / width;
    if (columns < 1)
        columns = 1;
    int rows = (count + columns - 1) / columns;

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < columns; c++)
        {
            int i = c * rows + r;
            if (i >= count)
                break;
            bool last = (c == columns - 1) || (i + rows >= count);
            PrintColoredName(&g_LsEntries[i], last ? 0 : width);
        }
        printf("\n");
    }
}

static int Cmd_Ls(int argc, char** argv)
{
    bool longFormat = false, showAll = false;
    const char* targets[SHELL_MAX_ARGS];
    int targetCount = 0;

    for (int i = 1; i < argc; i++)
    {
        if (argv[i][0] == '-' && argv[i][1] != '\0')
        {
            for (const char* f = argv[i] + 1; *f; f++)
            {
                if (*f == 'l') longFormat = true;
                else if (*f == 'a') showAll = true;
                else
                {
                    Shell_Error("ls", argv[i], "opzione sconosciuta (usa -l, -a)");
                    return 1;
                }
            }
        }
        else
            targets[targetCount++] = argv[i];
    }
    if (targetCount == 0)
        targets[targetCount++] = ".";

    int result = 0;
    for (int t = 0; t < targetCount; t++)
    {
        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("ls", targets[t], path))
            return 1;

        FAT_Entry entry;
        FAT_Status status = FAT_Lookup(path, &entry);
        if (status != FAT_OK)
        {
            PrintStatusError("ls", targets[t], status);
            result = 1;
            continue;
        }

        if (!entry.IsDirectory)
        {
            if (longFormat) Ls_PrintLong(&entry);
            else printf("%s\n", entry.Name);
            continue;
        }

        if (targetCount > 1)
            printf("%s:\n", targets[t]);

        LsContext ctx = { 0, showAll, false };
        status = FAT_ListDirectory(path, LsVisitor, &ctx);
        if (status != FAT_OK)
        {
            PrintStatusError("ls", targets[t], status);
            result = 1;
            continue;
        }
        Ls_Sort(ctx.Count);

        if (longFormat)
        {
            for (int i = 0; i < ctx.Count; i++)
                Ls_PrintLong(&g_LsEntries[i]);
        }
        else if (ctx.Count > 0)
            Ls_PrintColumns(ctx.Count);

        if (ctx.Truncated)
            printf("... (mostrati solo i primi %d)\n", LS_MAX_ENTRIES);
        if (targetCount > 1 && t < targetCount - 1)
            printf("\n");
    }
    return result;
}

static int MakeDirectoryWithParents(const char* path)
{
    // create every missing piece of the path, like "mkdir -p"
    char partial[PATH_MAX_LENGTH] = "";
    const char* p = path;
    while (*p)
    {
        const char* next = strchr(p + 1, '/');
        size_t len = next ? (size_t)(next - path) : strlen(path);
        memcpy(partial, path, len);
        partial[len] = '\0';

        FAT_Entry entry;
        FAT_Status status = FAT_Lookup(partial, &entry);
        if (status == FAT_ERR_NOT_FOUND)
            status = FAT_MakeDirectory(partial);
        else if (status == FAT_OK && !entry.IsDirectory)
            status = FAT_ERR_NOT_DIR;
        if (status != FAT_OK)
            return status;

        if (next == NULL)
            break;
        p = next;
    }
    return FAT_OK;
}

static int Cmd_Mkdir(int argc, char** argv)
{
    bool parents = false;
    int result = 0, done = 0;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-p") == 0)
        {
            parents = true;
            continue;
        }
        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("mkdir", argv[i], path))
            return 1;
        FAT_Status status = parents ? MakeDirectoryWithParents(path) : FAT_MakeDirectory(path);
        if (status != FAT_OK)
        {
            PrintStatusError("mkdir", argv[i], status);
            result = 1;
        }
        done++;
    }
    if (done == 0)
    {
        Shell_Error("mkdir", NULL, "manca il nome della cartella");
        return 1;
    }
    return result;
}

static int Cmd_Rmdir(int argc, char** argv)
{
    if (argc < 2)
    {
        Shell_Error("rmdir", NULL, "manca il nome della cartella");
        return 1;
    }
    int result = 0;
    for (int i = 1; i < argc; i++)
    {
        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("rmdir", argv[i], path))
            return 1;

        FAT_Entry entry;
        FAT_Status status = FAT_Lookup(path, &entry);
        if (status == FAT_OK && !entry.IsDirectory)
            status = FAT_ERR_NOT_DIR;
        if (status == FAT_OK && Path_IsInside(g_Cwd, path))
        {
            Shell_Error("rmdir", argv[i], "e' la cartella corrente (o la contiene)");
            result = 1;
            continue;
        }
        if (status == FAT_OK)
            status = FAT_Remove(path);
        if (status != FAT_OK)
        {
            PrintStatusError("rmdir", argv[i], status);
            result = 1;
        }
    }
    return result;
}

// tree: recursive, draws the branches with the box characters of code page 437
#define TREE_PREFIX_SIZE    96

typedef struct
{
    int Index;
    int Total;
    const char* Path;
    char* Prefix;
    int Directories;
    int Files;
    int Depth;
} TreeContext;

static bool CountVisitor(const FAT_Entry* entry, void* context)
{
    (*(int*)context)++;
    return true;
}

static void Tree_Print(const char* path, char* prefix, int depth, int* directories, int* files);

static bool TreeVisitor(const FAT_Entry* entry, void* context)
{
    TreeContext* ctx = (TreeContext*)context;
    bool last = (++ctx->Index == ctx->Total);

    // ├── or └──  (0xC3 = ├, 0xC0 = └, 0xC4 = ─, 0xB3 = │)
    printf("%s%c%c%c ", ctx->Prefix, last ? 0xC0 : 0xC3, 0xC4, 0xC4);
    PrintColoredName(entry, 0);
    printf("\n");

    if (entry->IsDirectory)
    {
        ctx->Directories++;
        char child[PATH_MAX_LENGTH];
        size_t prefixLen = strlen(ctx->Prefix);
        if (ctx->Depth < 16 && prefixLen + 5 < TREE_PREFIX_SIZE && JoinPath(child, ctx->Path, entry->Name))
        {
            char* p = ctx->Prefix + prefixLen;
            p[0] = last ? ' ' : (char)0xB3;
            p[1] = ' '; p[2] = ' '; p[3] = ' '; p[4] = '\0';
            int dirs = 0, fls = 0;
            Tree_Print(child, ctx->Prefix, ctx->Depth + 1, &dirs, &fls);
            ctx->Directories += dirs;
            ctx->Files += fls;
            ctx->Prefix[prefixLen] = '\0';
        }
    }
    else
        ctx->Files++;
    return true;
}

static void Tree_Print(const char* path, char* prefix, int depth, int* directories, int* files)
{
    int total = 0;
    FAT_ListDirectory(path, CountVisitor, &total);
    TreeContext ctx = { 0, total, path, prefix, 0, 0, depth };
    FAT_ListDirectory(path, TreeVisitor, &ctx);
    *directories = ctx.Directories;
    *files = ctx.Files;
}

static int Cmd_Tree(int argc, char** argv)
{
    char path[PATH_MAX_LENGTH];
    const char* arg = argc > 1 ? argv[1] : ".";
    if (!Shell_ResolvePath("tree", arg, path))
        return 1;

    FAT_Entry entry;
    FAT_Status status = FAT_Lookup(path, &entry);
    if (status == FAT_OK && !entry.IsDirectory)
        status = FAT_ERR_NOT_DIR;
    if (status != FAT_OK)
    {
        PrintStatusError("tree", arg, status);
        return 1;
    }

    VGA_SetColor(VGA_ATTR(VGA_LIGHT_BLUE, VGA_BLACK));
    printf("%s\n", path);
    VGA_SetColor(VGA_DEFAULT_COLOR);

    char prefix[TREE_PREFIX_SIZE] = "";
    int directories = 0, files = 0;
    Tree_Print(path, prefix, 0, &directories, &files);
    printf("\n%d cartelle, %d file\n", directories, files);
    return 0;
}

// --- files ---------------------------------------------------------------------------------

static int Cmd_Touch(int argc, char** argv)
{
    if (argc < 2)
    {
        Shell_Error("touch", NULL, "manca il nome del file");
        return 1;
    }
    int result = 0;
    for (int i = 1; i < argc; i++)
    {
        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("touch", argv[i], path))
            return 1;
        FAT_Status status = FAT_Touch(path);
        if (status != FAT_OK)
        {
            PrintStatusError("touch", argv[i], status);
            result = 1;
        }
    }
    return result;
}

typedef struct
{
    char Last;
    bool Any;
} CatContext;

static void CatCallback(const uint8_t* data, uint32_t length, void* context)
{
    CatContext* ctx = (CatContext*)context;
    for (uint32_t i = 0; i < length; i++)
        putc(data[i]);
    if (length > 0)
    {
        ctx->Last = data[length - 1];
        ctx->Any = true;
    }
}

static int Cmd_Cat(int argc, char** argv)
{
    if (argc < 2)
    {
        Shell_Error("cat", NULL, "manca il nome del file");
        return 1;
    }
    int result = 0;
    for (int i = 1; i < argc; i++)
    {
        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("cat", argv[i], path))
            return 1;
        CatContext ctx = { 0, false };
        FAT_Status status = FAT_ReadStream(path, CatCallback, &ctx);
        if (status != FAT_OK)
        {
            PrintStatusError("cat", argv[i], status);
            result = 1;
        }
        else if (ctx.Any && ctx.Last != '\n')
            putc('\n');         // so the prompt starts on a new line
    }
    return result;
}

// removes a directory and everything inside it
static FAT_Status RemoveRecursive(const char* path);

typedef struct
{
    char Name[FAT_MAX_NAME + 1];
    bool Found;
} FirstChildContext;

static bool FirstChildVisitor(const FAT_Entry* entry, void* context)
{
    FirstChildContext* ctx = (FirstChildContext*)context;
    strlcpy(ctx->Name, entry->Name, sizeof(ctx->Name));
    ctx->Found = true;
    return false;
}

static FAT_Status RemoveRecursive(const char* path)
{
    FAT_Entry entry;
    FAT_Status status = FAT_Lookup(path, &entry);
    if (status != FAT_OK)
        return status;

    if (entry.IsDirectory)
    {
        // remove the first child, again and again, until the directory is empty
        for (;;)
        {
            FirstChildContext ctx = { "", false };
            status = FAT_ListDirectory(path, FirstChildVisitor, &ctx);
            if (status != FAT_OK)
                return status;
            if (!ctx.Found)
                break;

            char child[PATH_MAX_LENGTH];
            if (!JoinPath(child, path, ctx.Name))
                return FAT_ERR_INVALID;
            status = RemoveRecursive(child);
            if (status != FAT_OK)
                return status;
        }
    }
    return FAT_Remove(path);
}

static int Cmd_Rm(int argc, char** argv)
{
    bool recursive = false;
    int result = 0, done = 0;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "-rf") == 0 || strcmp(argv[i], "-R") == 0)
        {
            recursive = true;
            continue;
        }
        done++;

        char path[PATH_MAX_LENGTH];
        if (!Shell_ResolvePath("rm", argv[i], path))
            return 1;

        FAT_Entry entry;
        FAT_Status status = FAT_Lookup(path, &entry);
        if (status == FAT_OK && strcmp(path, "/") == 0)
            status = FAT_ERR_INVALID;
        if (status == FAT_OK && entry.IsDirectory)
        {
            if (!recursive)
            {
                Shell_Error("rm", argv[i], "e' una cartella (usa rm -r o rmdir)");
                result = 1;
                continue;
            }
            if (Path_IsInside(g_Cwd, path))
            {
                Shell_Error("rm", argv[i], "e' la cartella corrente (o la contiene)");
                result = 1;
                continue;
            }
            status = RemoveRecursive(path);
        }
        else if (status == FAT_OK)
            status = FAT_Remove(path);

        if (status != FAT_OK)
        {
            PrintStatusError("rm", argv[i], status);
            result = 1;
        }
    }
    if (done == 0)
    {
        Shell_Error("rm", NULL, "manca il nome del file");
        return 1;
    }
    return result;
}

// if "dest" is an existing directory, the file goes inside it with its name
static bool DestinationPath(const char* command, const char* source, const char* destArg, char* dest)
{
    if (!Shell_ResolvePath(command, destArg, dest))
        return false;
    FAT_Entry entry;
    if (FAT_Lookup(dest, &entry) == FAT_OK && entry.IsDirectory)
    {
        char inside[PATH_MAX_LENGTH];
        if (!JoinPath(inside, dest, BaseName(source)))
        {
            Shell_Error(command, destArg, "percorso troppo lungo");
            return false;
        }
        strlcpy(dest, inside, PATH_MAX_LENGTH);
    }
    return true;
}

static int Cmd_Mv(int argc, char** argv)
{
    if (argc != 3)
    {
        Shell_Error("mv", NULL, "uso: mv origine destinazione");
        return 1;
    }
    char source[PATH_MAX_LENGTH], dest[PATH_MAX_LENGTH];
    if (!Shell_ResolvePath("mv", argv[1], source) || !DestinationPath("mv", source, argv[2], dest))
        return 1;

    if (Path_IsInside(g_Cwd, source) && strcmp(source, "/") != 0)
    {
        FAT_Entry entry;
        if (FAT_Lookup(source, &entry) == FAT_OK && entry.IsDirectory)
        {
            Shell_Error("mv", argv[1], "e' la cartella corrente (o la contiene)");
            return 1;
        }
    }

    FAT_Status status = FAT_Rename(source, dest);
    if (status != FAT_OK)
    {
        PrintStatusError("mv", argv[1], status);
        return 1;
    }
    return 0;
}

static int Cmd_Cp(int argc, char** argv)
{
    if (argc != 3)
    {
        Shell_Error("cp", NULL, "uso: cp origine destinazione");
        return 1;
    }
    char source[PATH_MAX_LENGTH], dest[PATH_MAX_LENGTH];
    if (!Shell_ResolvePath("cp", argv[1], source) || !DestinationPath("cp", source, argv[2], dest))
        return 1;
    if (strcasecmp(source, dest) == 0)
    {
        Shell_Error("cp", argv[1], "origine e destinazione sono lo stesso file");
        return 1;
    }

    uint32_t size;
    FAT_Status status = FAT_ReadFile(source, g_FileBuffer, sizeof(g_FileBuffer), &size);
    if (status != FAT_OK)
    {
        PrintStatusError("cp", argv[1], status);
        return 1;
    }
    status = FAT_WriteFile(dest, g_FileBuffer, size);
    if (status != FAT_OK)
    {
        PrintStatusError("cp", argv[2], status);
        return 1;
    }
    return 0;
}

static int Cmd_Edit(int argc, char** argv)
{
    if (argc != 2)
    {
        Shell_Error(argv[0], NULL, "uso: edit nomefile");
        return 1;
    }
    char path[PATH_MAX_LENGTH];
    if (!Shell_ResolvePath(argv[0], argv[1], path))
        return 1;
    return Editor_Run(path);
}

static int Cmd_Echo(int argc, char** argv)
{
    int first = 1;
    bool newline = true;
    if (argc > 1 && strcmp(argv[1], "-n") == 0)
    {
        newline = false;
        first = 2;
    }
    for (int i = first; i < argc; i++)
        printf(i > first ? " %s" : "%s", argv[i]);
    if (newline)
        printf("\n");
    return 0;
}

// --- system information ------------------------------------------------------------------

static const char* g_Weekdays[] = { "domenica", "lunedi'", "martedi'", "mercoledi'", "giovedi'", "venerdi'", "sabato" };
static const char* g_Months[] = { "gennaio", "febbraio", "marzo", "aprile", "maggio", "giugno", "luglio",
                                  "agosto", "settembre", "ottobre", "novembre", "dicembre" };

// day of the week (0 = Sunday), Sakamoto's algorithm
static int DayOfWeek(int y, int m, int d)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int Cmd_Date(int argc, char** argv)
{
    DateTime dt;
    RTC_Read(&dt);
    if (dt.Month < 1 || dt.Month > 12)
    {
        Shell_Error("date", NULL, "orologio non valido");
        return 1;
    }
    printf("%s %d %s %d, %02d:%02d:%02d\n", g_Weekdays[DayOfWeek(dt.Year, dt.Month, dt.Day)],
           dt.Day, g_Months[dt.Month - 1], dt.Year, dt.Hour, dt.Minute, dt.Second);
    return 0;
}

static int Cmd_Uptime(int argc, char** argv)
{
    uint32_t s = PIT_GetUptimeSeconds();
    printf("acceso da %u ore, %u minuti, %u secondi\n", s / 3600, (s / 60) % 60, s % 60);
    return 0;
}

extern char __end[];        // end of the kernel (from linker.ld)

static int Cmd_Free(int argc, char** argv)
{
    MemoryInfo* mem = &g_ShellBootParams->Memory;
    uint64_t usable = 0, reserved = 0;
    for (int i = 0; i < mem->RegionCount; i++)
    {
        if (mem->Regions[i].Type == 1)
            usable += mem->Regions[i].Length;
        else
            reserved += mem->Regions[i].Length;
    }
    uint32_t kernelSize = (uint32_t)__end - 0x100000;

    printf("RAM utilizzabile:  "); PrintSize(usable);   printf("\n");
    printf("Zone riservate:    "); PrintSize(reserved); printf("\n");
    printf("Kernel DACOS:      "); PrintSize(kernelSize); printf(" (da 0x100000 a 0x%x)\n", (uint32_t)__end);
    printf("\nMappa della memoria (dal BIOS, E820):\n");
    for (int i = 0; i < mem->RegionCount; i++)
    {
        MemoryRegion* r = &mem->Regions[i];
        printf("  0x%08llx - 0x%08llx  %s\n", r->Begin, r->Begin + r->Length - 1,
               r->Type == 1 ? "utilizzabile" : r->Type == 3 ? "ACPI (recuperabile)" : "riservata");
    }
    return 0;
}

static int Cmd_Df(int argc, char** argv)
{
    if (!FAT_IsMounted())
    {
        PrintStatusError("df", NULL, FAT_ERR_NOT_MOUNTED);
        return 1;
    }
    uint64_t total = FAT_GetTotalBytes(), free = FAT_GetFreeBytes();
    printf("Disco:       %s (%u MB)\n", ATA_GetModel(), ATA_GetSectorCount() / 2048);
    printf("File system: FAT%d, etichetta \"%s\", cluster da %u byte\n",
           FAT_GetType(), FAT_GetVolumeLabel(), FAT_GetClusterSize());
    printf("Totale:      "); PrintSize(total); printf("\n");
    printf("Usato:       "); PrintSize(total - free);
    printf(" (%u%%)\n", total ? (uint32_t)((total - free) * 100 / total) : 0);
    printf("Libero:      "); PrintSize(free); printf("\n");
    return 0;
}

static int Cmd_Keymap(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("tastiera attuale: %s (cambia con: keymap it | keymap us)\n",
               Keyboard_GetKeymap() == KEYMAP_IT ? "it (italiana)" : "us (americana)");
        return 0;
    }
    if (strcmp(argv[1], "it") == 0)
        Keyboard_SetKeymap(KEYMAP_IT);
    else if (strcmp(argv[1], "us") == 0)
        Keyboard_SetKeymap(KEYMAP_US);
    else
    {
        Shell_Error("keymap", argv[1], "layout sconosciuto (it o us)");
        return 1;
    }
    return 0;
}

static int Cmd_History(int argc, char** argv)
{
    for (int i = 0; i < Shell_HistoryCount(); i++)
        printf("%4d  %s\n", i + 1, Shell_HistoryGet(i));
    return 0;
}

static int Cmd_Uname(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "-a") == 0)
        printf("DACOS dacos %s i686 (kernel C/assembly, bootloader nanobyte)\n", DACOS_VERSION);
    else
        printf("DACOS\n");
    return 0;
}

static int Cmd_Whoami(int argc, char** argv)
{
    printf("dacos\n");
    return 0;
}

static int Cmd_Neofetch(int argc, char** argv)
{
    Neofetch_Run();
    return 0;
}

static int Cmd_Reboot(int argc, char** argv)
{
    printf("Riavvio...\n");
    Power_Reboot();
    return 0;
}

static int Cmd_Shutdown(int argc, char** argv)
{
    printf("Spegnimento...\n");
    Power_Shutdown();
    // still on: no known method worked
    VGA_SetColor(VGA_ATTR(VGA_YELLOW, VGA_BLACK));
    printf("Ora puoi spegnere il computer in sicurezza.\n");
    for (;;)
        __asm__ volatile ("cli; hlt");
    return 0;
}

// --- the command table -------------------------------------------------------------------

const Command g_Commands[] = {
    { "help",     Cmd_Help,     "help [comando]",         "mostra i comandi disponibili" },
    { "clear",    Cmd_Clear,    "clear",                  "pulisce lo schermo" },
    { "ls",       Cmd_Ls,       "ls [-l] [-a] [percorso...]", "elenca file e cartelle (-l dettagli, -a anche i nascosti)" },
    { "cd",       Cmd_Cd,       "cd [cartella]",          "cambia cartella (senza argomenti: torna a /)" },
    { "pwd",      Cmd_Pwd,      "pwd",                    "mostra la cartella corrente" },
    { "mkdir",    Cmd_Mkdir,    "mkdir [-p] cartella...", "crea una cartella (-p anche quelle intermedie)" },
    { "rmdir",    Cmd_Rmdir,    "rmdir cartella...",      "cancella una cartella vuota" },
    { "touch",    Cmd_Touch,    "touch file...",          "crea un file vuoto (o ne aggiorna la data)" },
    { "create",   Cmd_Touch,    "create file...",         "come touch" },
    { "cat",      Cmd_Cat,      "cat file...",            "mostra il contenuto di un file" },
    { "type",     Cmd_Cat,      "type file...",           "come cat" },
    { "edit",     Cmd_Edit,     "edit file",              "apre l'editor di testo (crea il file se non c'e')" },
    { "nano",     Cmd_Edit,     "nano file",              "come edit" },
    { "rm",       Cmd_Rm,       "rm [-r] percorso...",    "cancella file (-r anche cartelle con tutto dentro)" },
    { "delete",   Cmd_Rm,       "delete [-r] percorso...", "come rm" },
    { "mv",       Cmd_Mv,       "mv origine destinazione", "rinomina o sposta un file o una cartella" },
    { "rename",   Cmd_Mv,       "rename vecchio nuovo",   "come mv" },
    { "cp",       Cmd_Cp,       "cp origine destinazione", "copia un file (max 64 KB)" },
    { "tree",     Cmd_Tree,     "tree [cartella]",        "mostra l'albero di file e cartelle" },
    { "echo",     Cmd_Echo,     "echo [-n] testo...",     "scrive il testo (con > file lo salva in un file)" },
    { "neofetch", Cmd_Neofetch, "neofetch",               "informazioni sul sistema, con la papera" },
    { "date",     Cmd_Date,     "date",                   "data e ora" },
    { "uptime",   Cmd_Uptime,   "uptime",                 "da quanto tempo e' acceso il sistema" },
    { "free",     Cmd_Free,     "free",                   "memoria RAM e mappa della memoria" },
    { "df",       Cmd_Df,       "df",                     "spazio sul disco" },
    { "uname",    Cmd_Uname,    "uname [-a]",             "nome e versione del sistema" },
    { "whoami",   Cmd_Whoami,   "whoami",                 "nome dell'utente" },
    { "history",  Cmd_History,  "history",                "comandi scritti finora" },
    { "keymap",   Cmd_Keymap,   "keymap [it|us]",         "cambia il layout della tastiera" },
    { "reboot",   Cmd_Reboot,   "reboot",                 "riavvia il computer" },
    { "shutdown", Cmd_Shutdown, "shutdown",               "spegne il computer" },
};

const int g_CommandCount = sizeof(g_Commands) / sizeof(g_Commands[0]);
