#include "shell_internal.h"
#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <arch/i686/vga_text.h>
#include <arch/i686/keyboard.h>

// A small full-screen text editor, in the style of nano.
//
//  rows 0..22  the text
//  row 23      status bar: file name, modified, line/column
//  row 24      keys / messages
//
// The whole file is kept in g_FileBuffer (max 64 KB) as plain text with '\n'.

#define TEXT_ROWS       (VGA_HEIGHT - 2)
#define STATUS_ROW      (VGA_HEIGHT - 2)
#define MESSAGE_ROW     (VGA_HEIGHT - 1)
#define TAB_SPACES      4

#define COLOR_TEXT      VGA_DEFAULT_COLOR
#define COLOR_STATUS    VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY)
#define COLOR_KEY       VGA_ATTR(VGA_BLACK, VGA_LIGHT_GRAY)
#define COLOR_HINT      VGA_ATTR(VGA_LIGHT_GRAY, VGA_BLACK)
#define COLOR_MESSAGE   VGA_ATTR(VGA_YELLOW, VGA_BLACK)
#define COLOR_EMPTY     VGA_ATTR(VGA_BLUE, VGA_BLACK)

typedef struct
{
    char* Text;
    uint32_t Length;
    uint32_t Capacity;
    uint32_t Cursor;            // position in the text
    uint32_t TopLine;           // first line shown
    uint32_t LeftColumn;        // horizontal scroll
    uint32_t WantedColumn;      // column to keep when moving up/down
    bool Modified;
    bool QuitConfirm;
    char Path[PATH_MAX_LENGTH];
    char Message[VGA_WIDTH + 1];
} Editor;

static Editor g_Editor;

// --- position helpers -------------------------------------------------------------------

static uint32_t LineStart(Editor* ed, uint32_t pos)
{
    while (pos > 0 && ed->Text[pos - 1] != '\n')
        pos--;
    return pos;
}

static uint32_t LineEnd(Editor* ed, uint32_t pos)
{
    while (pos < ed->Length && ed->Text[pos] != '\n')
        pos++;
    return pos;
}

static uint32_t LineOf(Editor* ed, uint32_t pos)
{
    uint32_t line = 0;
    for (uint32_t i = 0; i < pos; i++)
        if (ed->Text[i] == '\n')
            line++;
    return line;
}

static uint32_t LineCount(Editor* ed)
{
    return LineOf(ed, ed->Length) + 1;
}

static uint32_t StartOfLine(Editor* ed, uint32_t line)
{
    uint32_t pos = 0;
    while (line > 0 && pos < ed->Length)
    {
        if (ed->Text[pos++] == '\n')
            line--;
    }
    return pos;
}

static uint32_t ColumnOf(Editor* ed, uint32_t pos)
{
    return pos - LineStart(ed, pos);
}

// moves to "column" in the line that starts at "start" (or to its end if shorter)
static uint32_t PositionInLine(Editor* ed, uint32_t start, uint32_t column)
{
    uint32_t end = LineEnd(ed, start);
    return start + column < end ? start + column : end;
}

// --- drawing ---------------------------------------------------------------------------------

static void DrawText(int x, int y, const char* text, uint8_t color)
{
    for (; *text && x < VGA_WIDTH; text++, x++)
        VGA_PutCharAt(x, y, *text, color);
}

static void FillRow(int y, int from, uint8_t color)
{
    for (int x = from; x < VGA_WIDTH; x++)
        VGA_PutCharAt(x, y, ' ', color);
}

static void Editor_Draw(Editor* ed)
{
    uint32_t cursorLine = LineOf(ed, ed->Cursor);
    uint32_t cursorColumn = ColumnOf(ed, ed->Cursor);

    // keep the cursor on the screen
    if (cursorLine < ed->TopLine)
        ed->TopLine = cursorLine;
    if (cursorLine >= ed->TopLine + TEXT_ROWS)
        ed->TopLine = cursorLine - TEXT_ROWS + 1;
    if (cursorColumn < ed->LeftColumn)
        ed->LeftColumn = cursorColumn;
    if (cursorColumn >= ed->LeftColumn + VGA_WIDTH)
        ed->LeftColumn = cursorColumn - VGA_WIDTH + 1;

    // text
    uint32_t pos = StartOfLine(ed, ed->TopLine);
    uint32_t lines = LineCount(ed);
    for (int row = 0; row < TEXT_ROWS; row++)
    {
        if (ed->TopLine + row >= lines)
        {
            VGA_PutCharAt(0, row, '~', COLOR_EMPTY);     // after the end of the file
            FillRow(row, 1, COLOR_TEXT);
            continue;
        }

        uint32_t end = LineEnd(ed, pos);
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            uint32_t i = pos + ed->LeftColumn + x;
            char c = (i < end) ? ed->Text[i] : ' ';
            if (c == '\t' || c == '\r')
                c = ' ';
            VGA_PutCharAt(x, row, c, COLOR_TEXT);
        }
        pos = end + 1;
    }

    // status bar
    char status[VGA_WIDTH * 2];
    FillRow(STATUS_ROW, 0, COLOR_STATUS);
    strlcpy(status, " DACOS edit  ", sizeof(status));
    strlcat(status, ed->Path, sizeof(status));
    if (ed->Modified)
        strlcat(status, "  [modificato]", sizeof(status));
    DrawText(0, STATUS_ROW, status, COLOR_STATUS);

    char position[40];
    int len = 0;
    // "riga X, col Y" without printf (it would go to the screen)
    const char* label = "riga ";
    while (*label) position[len++] = *label++;
    char digits[12];
    int d = 0;
    for (uint32_t v = cursorLine + 1; v > 0 || d == 0; v /= 10) digits[d++] = '0' + v % 10;
    while (d > 0) position[len++] = digits[--d];
    label = ", col ";
    while (*label) position[len++] = *label++;
    for (uint32_t v = cursorColumn + 1; v > 0 || d == 0; v /= 10) digits[d++] = '0' + v % 10;
    while (d > 0) position[len++] = digits[--d];
    position[len++] = ' ';
    position[len] = '\0';
    DrawText(VGA_WIDTH - len, STATUS_ROW, position, COLOR_STATUS);

    // message or key help
    FillRow(MESSAGE_ROW, 0, COLOR_HINT);
    if (ed->Message[0])
        DrawText(1, MESSAGE_ROW, ed->Message, COLOR_MESSAGE);
    else
    {
        static const char* keys[][2] = {
            { "^S", "Salva" }, { "^Q", "Esci" }, { "^K", "Cancella riga" },
            { "Tab", "4 spazi" }, { "PagSu/PagGiu", "scorri" },
        };
        int x = 1;
        for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        {
            DrawText(x, MESSAGE_ROW, keys[i][0], COLOR_KEY);
            x += strlen(keys[i][0]) + 1;
            DrawText(x, MESSAGE_ROW, keys[i][1], COLOR_HINT);
            x += strlen(keys[i][1]) + 3;
        }
    }

    VGA_SetCursor(cursorColumn - ed->LeftColumn, cursorLine - ed->TopLine);
}

// --- editing ------------------------------------------------------------------------------------

static void SetMessage(Editor* ed, const char* message)
{
    strlcpy(ed->Message, message, sizeof(ed->Message));
}

static bool InsertChar(Editor* ed, char c)
{
    if (ed->Length >= ed->Capacity)
    {
        SetMessage(ed, "File troppo grande: massimo 64 KB");
        return false;
    }
    memmove(ed->Text + ed->Cursor + 1, ed->Text + ed->Cursor, ed->Length - ed->Cursor);
    ed->Text[ed->Cursor++] = c;
    ed->Length++;
    ed->Modified = true;
    return true;
}

static void DeleteAt(Editor* ed, uint32_t pos, uint32_t count)
{
    if (pos >= ed->Length)
        return;
    if (pos + count > ed->Length)
        count = ed->Length - pos;
    memmove(ed->Text + pos, ed->Text + pos + count, ed->Length - pos - count);
    ed->Length -= count;
    ed->Modified = true;
}

static void Editor_Save(Editor* ed)
{
    FAT_Status status = FAT_WriteFile(ed->Path, ed->Text, ed->Length);
    if (status != FAT_OK)
    {
        char message[VGA_WIDTH + 1] = "Errore nel salvataggio: ";
        strlcat(message, FAT_StatusString(status), sizeof(message));
        SetMessage(ed, message);
        return;
    }
    ed->Modified = false;

    char message[VGA_WIDTH + 1] = "Salvato: ";
    char digits[12];
    int d = 0;
    for (uint32_t v = ed->Length; v > 0 || d == 0; v /= 10) digits[d++] = '0' + v % 10;
    int len = strlen(message);
    while (d > 0) message[len++] = digits[--d];
    message[len] = '\0';
    strlcat(message, " byte", sizeof(message));
    SetMessage(ed, message);
}

int Editor_Run(const char* path)
{
    Editor* ed = &g_Editor;
    memset(ed, 0, sizeof(*ed));
    ed->Text = (char*)g_FileBuffer;
    ed->Capacity = FILE_BUFFER_SIZE;
    strlcpy(ed->Path, path, sizeof(ed->Path));

    // load the file (if it exists)
    FAT_Entry entry;
    FAT_Status status = FAT_Lookup(path, &entry);
    if (status == FAT_OK)
    {
        if (entry.IsDirectory)
        {
            Shell_Error("edit", path, FAT_StatusString(FAT_ERR_IS_DIR));
            return 1;
        }
        status = FAT_ReadFile(path, ed->Text, ed->Capacity, &ed->Length);
        if (status != FAT_OK)
        {
            Shell_Error("edit", path, FAT_StatusString(status));
            return 1;
        }
    }
    else if (status == FAT_ERR_NOT_FOUND)
    {
        // check that the directory exists, so the file can be saved later
        char parent[PATH_MAX_LENGTH], name[FAT_MAX_NAME + 1];
        Path_Split(path, parent, sizeof(parent), name, sizeof(name));
        FAT_Entry dir;
        FAT_Status dirStatus = FAT_Lookup(parent, &dir);
        if (dirStatus != FAT_OK || !dir.IsDirectory)
        {
            Shell_Error("edit", parent, FAT_StatusString(dirStatus != FAT_OK ? dirStatus : FAT_ERR_NOT_DIR));
            return 1;
        }
        SetMessage(ed, "Nuovo file: premi Ctrl+S per salvarlo");
    }
    else
    {
        Shell_Error("edit", path, FAT_StatusString(status));
        return 1;
    }

    uint8_t oldColor = VGA_GetColor();
    VGA_SetColor(VGA_DEFAULT_COLOR);

    for (;;)
    {
        Editor_Draw(ed);
        int key = Keyboard_GetKey();

        bool keepMessage = false;
        bool keepColumn = false;
        if (key != KEY_CTRL('q') && key != KEY_CTRL('x') && key != KEY_ESCAPE)
            ed->QuitConfirm = false;

        uint32_t lineStart = LineStart(ed, ed->Cursor);
        uint32_t column = ed->Cursor - lineStart;

        switch (key)
        {
            case KEY_CTRL('s'):
                Editor_Save(ed);
                keepMessage = true;
                break;

            case KEY_CTRL('q'):
            case KEY_CTRL('x'):
            case KEY_ESCAPE:
                if (ed->Modified && !ed->QuitConfirm)
                {
                    SetMessage(ed, "Modifiche non salvate! Ctrl+Q di nuovo per uscire comunque, Ctrl+S per salvare");
                    ed->QuitConfirm = true;
                    keepMessage = true;
                    break;
                }
                VGA_SetColor(oldColor);
                VGA_clrscr();
                return 0;

            case KEY_LEFT:
                if (ed->Cursor > 0) ed->Cursor--;
                break;

            case KEY_RIGHT:
                if (ed->Cursor < ed->Length) ed->Cursor++;
                break;

            case KEY_UP:
                if (lineStart > 0)
                    ed->Cursor = PositionInLine(ed, LineStart(ed, lineStart - 1), ed->WantedColumn);
                keepColumn = true;
                break;

            case KEY_DOWN:
            {
                uint32_t end = LineEnd(ed, ed->Cursor);
                if (end < ed->Length)
                    ed->Cursor = PositionInLine(ed, end + 1, ed->WantedColumn);
                keepColumn = true;
                break;
            }

            case KEY_PAGE_UP:
            case KEY_PAGE_DOWN:
            {
                uint32_t line = LineOf(ed, ed->Cursor);
                uint32_t lines = LineCount(ed);
                if (key == KEY_PAGE_UP)
                    line = line > TEXT_ROWS ? line - TEXT_ROWS : 0;
                else
                    line = line + TEXT_ROWS < lines ? line + TEXT_ROWS : lines - 1;
                ed->Cursor = PositionInLine(ed, StartOfLine(ed, line), ed->WantedColumn);
                keepColumn = true;
                break;
            }

            case KEY_HOME:
            case KEY_CTRL('a'):
                ed->Cursor = lineStart;
                break;

            case KEY_END:
            case KEY_CTRL('e'):
                ed->Cursor = LineEnd(ed, ed->Cursor);
                break;

            case '\b':
                if (ed->Cursor > 0)
                {
                    ed->Cursor--;
                    DeleteAt(ed, ed->Cursor, 1);
                }
                break;

            case KEY_DELETE:
                DeleteAt(ed, ed->Cursor, 1);
                break;

            case KEY_CTRL('k'):
            {
                // delete the whole line (with its '\n')
                uint32_t end = LineEnd(ed, ed->Cursor);
                uint32_t count = end - lineStart + (end < ed->Length ? 1 : 0);
                if (count == 0 && lineStart > 0)
                {
                    lineStart--;        // empty last line: remove the '\n' before it
                    count = 1;
                }
                DeleteAt(ed, lineStart, count);
                ed->Cursor = lineStart;
                break;
            }

            case '\t':
                for (int i = 0; i < TAB_SPACES; i++)
                    InsertChar(ed, ' ');
                keepMessage = ed->Length >= ed->Capacity;
                break;

            case '\n':
                InsertChar(ed, '\n');
                keepMessage = ed->Length >= ed->Capacity;
                break;

            default:
                if (key >= 32 && key < 256 && key != 127)
                    keepMessage = !InsertChar(ed, (char)key);
                break;
        }

        if (!keepMessage)
            ed->Message[0] = '\0';
        if (!keepColumn)
            ed->WantedColumn = ColumnOf(ed, ed->Cursor);
        (void)column;
    }
}
