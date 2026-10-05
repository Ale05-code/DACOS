#include "shell.h"
#include "shell_internal.h"
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <debug.h>
#include <hal/vfs.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/vga_text.h>
#include <arch/i686/e9.h>

// dsh, the DACOS shell: reads a line, splits it into words and runs the command.

char g_Cwd[PATH_MAX_LENGTH] = "/";
BootParams* g_ShellBootParams = NULL;
uint8_t g_FileBuffer[FILE_BUFFER_SIZE];

static char g_CaptureBuffer[FILE_BUFFER_SIZE];

// --- history ---------------------------------------------------------------------

#define HISTORY_SIZE    32

static char g_History[HISTORY_SIZE][SHELL_MAX_LINE];
static int g_HistoryCount = 0;          // total number of commands ever saved
static int g_HistoryFirst = 0;          // index (in "ever saved") of the oldest one kept

static void History_Add(const char* line)
{
    if (line[0] == '\0')
        return;
    if (g_HistoryCount > 0 && strcmp(g_History[(g_HistoryCount - 1) % HISTORY_SIZE], line) == 0)
        return;     // same as the previous one
    strlcpy(g_History[g_HistoryCount % HISTORY_SIZE], line, SHELL_MAX_LINE);
    g_HistoryCount++;
    if (g_HistoryCount - g_HistoryFirst > HISTORY_SIZE)
        g_HistoryFirst = g_HistoryCount - HISTORY_SIZE;
}

int Shell_HistoryCount()
{
    return g_HistoryCount - g_HistoryFirst;
}

const char* Shell_HistoryGet(int index)
{
    return g_History[(g_HistoryFirst + index) % HISTORY_SIZE];
}

// --- helpers used by the commands ---------------------------------------------------

void Shell_Error(const char* command, const char* arg, const char* message)
{
    uint8_t color = VGA_GetColor();
    VGA_SetColor(VGA_ATTR(VGA_LIGHT_RED, VGA_BLACK));
    if (arg != NULL)
        fprintf(VFS_FD_STDERR, "%s: %s: %s\n", command, arg, message);
    else
        fprintf(VFS_FD_STDERR, "%s: %s\n", command, message);
    VGA_SetColor(color);
}

bool Shell_ResolvePath(const char* command, const char* arg, char* out)
{
    if (!Path_Resolve(g_Cwd, arg, out, PATH_MAX_LENGTH))
    {
        Shell_Error(command, arg, "percorso troppo lungo");
        return false;
    }
    return true;
}

const Command* Shell_FindCommand(const char* name)
{
    for (int i = 0; i < g_CommandCount; i++)
        if (strcmp(g_Commands[i].Name, name) == 0)
            return &g_Commands[i];
    return NULL;
}

// --- prompt and line editor -----------------------------------------------------------

static void Shell_PrintPrompt()
{
    VGA_SetColor(VGA_ATTR(VGA_LIGHT_GREEN, VGA_BLACK));
    printf("dacos@DACOS");
    VGA_SetColor(VGA_DEFAULT_COLOR);
    printf(":");
    VGA_SetColor(VGA_ATTR(VGA_LIGHT_BLUE, VGA_BLACK));
    printf("%s", g_Cwd);
    VGA_SetColor(VGA_DEFAULT_COLOR);
    printf("$ ");
}

typedef struct
{
    char* Buffer;
    int Length;
    int Cursor;
    int StartX, StartY;         // where the input starts on the screen
    int PreviousLength;         // to erase leftovers when the line gets shorter
} LineEditor;

static void Line_Redraw(LineEditor* ed)
{
    VGA_SetCursor(ed->StartX, ed->StartY);
    int start = ed->StartY * VGA_WIDTH + ed->StartX;

    int toWrite = ed->Length > ed->PreviousLength ? ed->Length : ed->PreviousLength;
    for (int i = 0; i < toWrite; i++)
        VGA_putc(i < ed->Length ? ed->Buffer[i] : ' ');
    ed->PreviousLength = ed->Length;

    // if the screen scrolled while writing, the start of the line moved up
    int x, y;
    VGA_GetCursor(&x, &y);
    int expected = start + toWrite;
    int actual = y * VGA_WIDTH + x;
    if (expected > actual)
        start -= ((expected - actual) / VGA_WIDTH) * VGA_WIDTH;
    ed->StartX = start % VGA_WIDTH;
    ed->StartY = start / VGA_WIDTH;

    int position = start + ed->Cursor;
    VGA_SetCursor(position % VGA_WIDTH, position / VGA_WIDTH);
}

static void Line_Set(LineEditor* ed, const char* text)
{
    strlcpy(ed->Buffer, text, SHELL_MAX_LINE);
    ed->Length = strlen(ed->Buffer);
    ed->Cursor = ed->Length;
    Line_Redraw(ed);
}

static void Line_Insert(LineEditor* ed, const char* text)
{
    for (; *text; text++)
    {
        if (ed->Length >= SHELL_MAX_LINE - 1)
            break;
        memmove(ed->Buffer + ed->Cursor + 1, ed->Buffer + ed->Cursor, ed->Length - ed->Cursor);
        ed->Buffer[ed->Cursor++] = *text;
        ed->Length++;
    }
    ed->Buffer[ed->Length] = '\0';
}

// starts a new prompt (after printing something below the line)
static void Line_Restart(LineEditor* ed)
{
    Shell_PrintPrompt();
    VGA_GetCursor(&ed->StartX, &ed->StartY);
    ed->PreviousLength = 0;
    Line_Redraw(ed);
}

// --- Tab completion ----------------------------------------------------------------------

#define MAX_MATCHES     64

typedef struct
{
    const char* Prefix;
    char Matches[MAX_MATCHES][FAT_MAX_NAME + 2];
    int Count;
} CompletionContext;

static bool StartsWithNoCase(const char* str, const char* prefix)
{
    for (; *prefix; str++, prefix++)
        if (tolower(*str) != tolower(*prefix))
            return false;
    return true;
}

static bool CompletionVisitor(const FAT_Entry* entry, void* context)
{
    CompletionContext* ctx = (CompletionContext*)context;
    if (ctx->Count < MAX_MATCHES && StartsWithNoCase(entry->Name, ctx->Prefix))
    {
        strlcpy(ctx->Matches[ctx->Count], entry->Name, FAT_MAX_NAME + 1);
        if (entry->IsDirectory)
            strlcat(ctx->Matches[ctx->Count], "/", FAT_MAX_NAME + 2);
        ctx->Count++;
    }
    return true;
}

static CompletionContext g_Completion;

static void Line_Complete(LineEditor* ed)
{
    // the word under the cursor
    int wordStart = ed->Cursor;
    while (wordStart > 0 && ed->Buffer[wordStart - 1] != ' ')
        wordStart--;

    bool isCommand = true;
    for (int i = 0; i < wordStart; i++)
        if (ed->Buffer[i] != ' ')
            isCommand = false;

    char word[SHELL_MAX_LINE];
    int wordLen = ed->Cursor - wordStart;
    memcpy(word, ed->Buffer + wordStart, wordLen);
    word[wordLen] = '\0';

    CompletionContext* ctx = &g_Completion;
    ctx->Count = 0;
    const char* prefix = word;

    if (isCommand)
    {
        ctx->Prefix = word;
        for (int i = 0; i < g_CommandCount && ctx->Count < MAX_MATCHES; i++)
            if (strncmp(g_Commands[i].Name, word, wordLen) == 0)
            {
                strlcpy(ctx->Matches[ctx->Count], g_Commands[i].Name, FAT_MAX_NAME + 1);
                strlcat(ctx->Matches[ctx->Count], " ", FAT_MAX_NAME + 2);
                ctx->Count++;
            }
    }
    else
    {
        // split "dir/part" → list "dir", look for names starting with "part"
        char dirPart[SHELL_MAX_LINE];
        char* slash = strrchr(word, '/');
        if (slash != NULL)
        {
            int len = slash - word + 1;
            memcpy(dirPart, word, len);
            dirPart[len] = '\0';
            prefix = slash + 1;
        }
        else
            strlcpy(dirPart, ".", sizeof(dirPart));

        char dirPath[PATH_MAX_LENGTH];
        if (!Path_Resolve(g_Cwd, dirPart, dirPath, sizeof(dirPath)))
            return;
        ctx->Prefix = prefix;
        FAT_ListDirectory(dirPath, CompletionVisitor, ctx);
    }

    if (ctx->Count == 0)
        return;

    int prefixLen = strlen(prefix);
    if (ctx->Count == 1)
    {
        Line_Insert(ed, ctx->Matches[0] + prefixLen);
        Line_Redraw(ed);
        return;
    }

    // several matches: complete the common part, otherwise show them all
    int common = strlen(ctx->Matches[0]);
    for (int i = 1; i < ctx->Count; i++)
    {
        int j = 0;
        while (j < common && tolower(ctx->Matches[i][j]) == tolower(ctx->Matches[0][j]))
            j++;
        common = j;
    }

    if (common > prefixLen)
    {
        char add[FAT_MAX_NAME + 2];
        memcpy(add, ctx->Matches[0] + prefixLen, common - prefixLen);
        add[common - prefixLen] = '\0';
        Line_Insert(ed, add);
        Line_Redraw(ed);
        return;
    }

    // print the list below the line, then the prompt again
    int end = ed->StartY * VGA_WIDTH + ed->StartX + ed->Length;
    VGA_SetCursor(end % VGA_WIDTH, end / VGA_WIDTH);
    putc('\n');
    for (int i = 0; i < ctx->Count; i++)
        printf("%s  ", ctx->Matches[i]);
    putc('\n');
    Line_Restart(ed);
}

// reads a line with editing, history and completion
static void Shell_ReadLine(char* buffer)
{
    LineEditor ed = { buffer, 0, 0, 0, 0, 0 };
    buffer[0] = '\0';
    VGA_GetCursor(&ed.StartX, &ed.StartY);

    int historyPos = Shell_HistoryCount();      // = "the line being written"
    char saved[SHELL_MAX_LINE] = "";

    for (;;)
    {
        int key = Keyboard_GetKey();

        switch (key)
        {
            case '\n':
            {
                int end = ed.StartY * VGA_WIDTH + ed.StartX + ed.Length;
                VGA_SetCursor(end % VGA_WIDTH, end / VGA_WIDTH);
                VGA_putc('\n');
                // copy of the line on the debug port, so the session can be followed there
                for (int i = 0; i < ed.Length; i++)
                    VFS_DebugPutChar(buffer[i]);
                e9_putc('\n');
                return;
            }

            case '\b':
                if (ed.Cursor > 0)
                {
                    memmove(buffer + ed.Cursor - 1, buffer + ed.Cursor, ed.Length - ed.Cursor + 1);
                    ed.Cursor--;
                    ed.Length--;
                }
                break;

            case KEY_DELETE:
                if (ed.Cursor < ed.Length)
                {
                    memmove(buffer + ed.Cursor, buffer + ed.Cursor + 1, ed.Length - ed.Cursor);
                    ed.Length--;
                }
                break;

            case KEY_LEFT:      if (ed.Cursor > 0) ed.Cursor--;             break;
            case KEY_RIGHT:     if (ed.Cursor < ed.Length) ed.Cursor++;     break;
            case KEY_HOME:
            case KEY_CTRL('a'): ed.Cursor = 0;                              break;
            case KEY_END:
            case KEY_CTRL('e'): ed.Cursor = ed.Length;                      break;

            case KEY_UP:
                if (historyPos > 0)
                {
                    if (historyPos == Shell_HistoryCount())
                        strlcpy(saved, buffer, sizeof(saved));
                    historyPos--;
                    Line_Set(&ed, Shell_HistoryGet(historyPos));
                }
                continue;

            case KEY_DOWN:
                if (historyPos < Shell_HistoryCount())
                {
                    historyPos++;
                    Line_Set(&ed, historyPos == Shell_HistoryCount() ? saved : Shell_HistoryGet(historyPos));
                }
                continue;

            case '\t':
                Line_Complete(&ed);
                continue;

            case KEY_CTRL('c'):
            {
                // cancel the line
                int end = ed.StartY * VGA_WIDTH + ed.StartX + ed.Length;
                VGA_SetCursor(end % VGA_WIDTH, end / VGA_WIDTH);
                printf("^C\n");
                buffer[0] = '\0';
                ed.Length = ed.Cursor = 0;
                Line_Restart(&ed);
                historyPos = Shell_HistoryCount();
                continue;
            }

            case KEY_CTRL('l'):
                VGA_clrscr();
                Line_Restart(&ed);
                continue;

            case KEY_CTRL('u'):
                // delete everything before the cursor
                memmove(buffer, buffer + ed.Cursor, ed.Length - ed.Cursor + 1);
                ed.Length -= ed.Cursor;
                ed.Cursor = 0;
                break;

            default:
                if (key >= 32 && key < 256 && key != 127)
                {
                    char text[2] = { (char)key, '\0' };
                    Line_Insert(&ed, text);
                }
                else
                    continue;       // other keys do nothing
                break;
        }

        buffer[ed.Length] = '\0';
        Line_Redraw(&ed);
    }
}

// --- parsing and execution -------------------------------------------------------------

typedef enum { REDIRECT_NONE, REDIRECT_WRITE, REDIRECT_APPEND } RedirectMode;

// splits the line in words (quotes group words: echo "ciao mondo")
// and finds a redirection "> file" or ">> file". Returns argc, or -1 on error.
static int Shell_Parse(char* line, char** argv, RedirectMode* redirect, char** redirectFile)
{
    static char words[SHELL_MAX_LINE * 2];
    char* out = words;
    int argc = 0;
    *redirect = REDIRECT_NONE;
    *redirectFile = NULL;
    bool expectFile = false;

    char* p = line;
    while (*p)
    {
        while (*p == ' ')
            p++;
        if (*p == '\0')
            break;

        // redirection operators (only when not inside quotes)
        if (*p == '>')
        {
            if (*redirect != REDIRECT_NONE)
                return -1;
            *redirect = (p[1] == '>') ? REDIRECT_APPEND : REDIRECT_WRITE;
            p += (p[1] == '>') ? 2 : 1;
            expectFile = true;
            continue;
        }

        char* word = out;
        bool quoted = false;
        while (*p && (quoted || *p != ' ') && (quoted || *p != '>'))
        {
            if (*p == '"' || *p == '\'')
            {
                char quote = *p++;
                while (*p && *p != quote)
                    *out++ = *p++;
                if (*p == quote)
                    p++;
                quoted = false;
                continue;
            }
            *out++ = *p++;
        }
        *out++ = '\0';

        if (expectFile)
        {
            *redirectFile = word;
            expectFile = false;
        }
        else if (argc < SHELL_MAX_ARGS - 1)
            argv[argc++] = word;
    }

    if (expectFile)         // "ls >" without a file name
        return -1;
    argv[argc] = NULL;
    return argc;
}

static void Shell_Execute(char* line)
{
    char* argv[SHELL_MAX_ARGS];
    RedirectMode redirect;
    char* redirectFile;

    int argc = Shell_Parse(line, argv, &redirect, &redirectFile);
    if (argc < 0)
    {
        Shell_Error("dsh", NULL, "redirezione non valida (uso: comando > file)");
        return;
    }
    if (argc == 0)
        return;

    const Command* command = Shell_FindCommand(argv[0]);
    if (command == NULL)
    {
        Shell_Error(argv[0], NULL, "comando non trovato (scrivi 'help')");
        return;
    }

    if (redirect == REDIRECT_NONE)
    {
        command->Function(argc, argv);
        return;
    }

    // "command > file": collect the output in a buffer, then write it to the file
    char path[PATH_MAX_LENGTH];
    if (!Shell_ResolvePath("dsh", redirectFile, path))
        return;

    VFS_StartCapture(g_CaptureBuffer, sizeof(g_CaptureBuffer));
    command->Function(argc, argv);
    uint32_t length = VFS_StopCapture();
    if (VFS_CaptureOverflow())
        Shell_Error("dsh", redirectFile, "output troppo lungo, salvati solo i primi 64 KB");

    const uint8_t* data = (const uint8_t*)g_CaptureBuffer;
    if (redirect == REDIRECT_APPEND)
    {
        uint32_t oldSize = 0;
        FAT_Status status = FAT_ReadFile(path, g_FileBuffer, sizeof(g_FileBuffer), &oldSize);
        if (status != FAT_OK && status != FAT_ERR_NOT_FOUND)
        {
            Shell_Error("dsh", redirectFile, FAT_StatusString(status));
            return;
        }
        if (status == FAT_ERR_NOT_FOUND)
            oldSize = 0;
        if (oldSize + length > sizeof(g_FileBuffer))
        {
            Shell_Error("dsh", redirectFile, FAT_StatusString(FAT_ERR_TOO_BIG));
            return;
        }
        memcpy(g_FileBuffer + oldSize, g_CaptureBuffer, length);
        data = g_FileBuffer;
        length += oldSize;
    }

    FAT_Status status = FAT_WriteFile(path, data, length);
    if (status != FAT_OK)
        Shell_Error("dsh", redirectFile, FAT_StatusString(status));
}

// --- main loop --------------------------------------------------------------------------

void Shell_Run(BootParams* bootParams)
{
    g_ShellBootParams = bootParams;

    VGA_SetColor(VGA_ATTR(VGA_YELLOW, VGA_BLACK));
    printf("Benvenuti in DACOS %s!\n", DACOS_VERSION);
    VGA_SetColor(VGA_DEFAULT_COLOR);
    printf("Scrivi 'help' per la lista dei comandi, 'neofetch' per le informazioni sul sistema.\n");
    if (!FAT_IsMounted())
    {
        VGA_SetColor(VGA_ATTR(VGA_LIGHT_RED, VGA_BLACK));
        printf("Attenzione: nessun disco ATA con file system FAT, i comandi sui file non funzionano.\n");
        printf("(La versione floppy non ha un disco: usa l'immagine 'disk', quella di default.)\n");
        VGA_SetColor(VGA_DEFAULT_COLOR);
    }
    printf("\n");

    char line[SHELL_MAX_LINE];
    for (;;)
    {
        Shell_PrintPrompt();
        Shell_ReadLine(line);
        History_Add(line);
        Shell_Execute(line);
    }
}
