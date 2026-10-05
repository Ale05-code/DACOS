#include "shell.h"
#include "shell_internal.h"
#include "ascii_art.h"
#include <stdio.h>
#include <string.h>
#include <arch/i686/vga_text.h>
#include <arch/i686/keyboard.h>
#include <arch/i686/pit.h>
#include <arch/i686/cpu.h>

// neofetch: the DACOS duck on the left, information about the system on the right.
// The drawing comes from theory/ascii_art.txt (see build_scripts/generate_ascii_art.py).

#define INFO_COLUMN     (ASCII_ART_WIDTH + 2)
#define INFO_WIDTH      (VGA_WIDTH - INFO_COLUMN - 1)   // -1: a full row would wrap
#define INFO_LINES      16

#define COLOR_LABEL     VGA_ATTR(VGA_YELLOW, VGA_BLACK)
#define COLOR_TITLE     VGA_ATTR(VGA_LIGHT_GREEN, VGA_BLACK)

typedef struct
{
    char Label[16];
    char Value[40];
} InfoLine;

static InfoLine g_Info[INFO_LINES];
static int g_InfoCount;

// tiny "sprintf" for numbers: appends the decimal value of n to str
static void AppendNumber(char* str, size_t size, uint32_t n)
{
    char digits[12];
    int d = 0;
    do {
        digits[d++] = '0' + n % 10;
        n /= 10;
    } while (n > 0);
    char tmp[12];
    int len = 0;
    while (d > 0)
        tmp[len++] = digits[--d];
    tmp[len] = '\0';
    strlcat(str, tmp, size);
}

static InfoLine* AddInfo(const char* label)
{
    InfoLine* line = &g_Info[g_InfoCount++];
    strlcpy(line->Label, label, sizeof(line->Label));
    line->Value[0] = '\0';
    return line;
}

static void CollectInfo()
{
    g_InfoCount = 0;
    InfoLine* line;

    line = AddInfo("OS");
    strlcpy(line->Value, "DACOS " DACOS_VERSION " i686", sizeof(line->Value));

    line = AddInfo("Kernel");
    strlcpy(line->Value, "dacos " DACOS_VERSION, sizeof(line->Value));

    uint32_t s = PIT_GetUptimeSeconds();
    line = AddInfo("Uptime");
    if (s >= 3600)
    {
        AppendNumber(line->Value, sizeof(line->Value), s / 3600);
        strlcat(line->Value, "h ", sizeof(line->Value));
    }
    AppendNumber(line->Value, sizeof(line->Value), (s / 60) % 60);
    strlcat(line->Value, "m ", sizeof(line->Value));
    AppendNumber(line->Value, sizeof(line->Value), s % 60);
    strlcat(line->Value, "s", sizeof(line->Value));

    line = AddInfo("Shell");
    strlcpy(line->Value, "dsh " DACOS_VERSION, sizeof(line->Value));

    line = AddInfo("Video");
    strlcpy(line->Value, "VGA testo 80x25", sizeof(line->Value));

    line = AddInfo("Tastiera");
    strlcpy(line->Value, Keyboard_GetKeymap() == KEYMAP_IT ? "it" : "us", sizeof(line->Value));

    char cpu[64];
    CPU_GetBrandString(cpu);
    line = AddInfo("CPU");
    strlcpy(line->Value, cpu, sizeof(line->Value));

    uint64_t usable = 0;
    MemoryInfo* mem = &g_ShellBootParams->Memory;
    for (int i = 0; i < mem->RegionCount; i++)
        if (mem->Regions[i].Type == 1)
            usable += mem->Regions[i].Length;
    line = AddInfo("Memoria");
    AppendNumber(line->Value, sizeof(line->Value), (uint32_t)(usable / (1024 * 1024)));
    strlcat(line->Value, " MB", sizeof(line->Value));

    if (FAT_IsMounted())
    {
        uint64_t total = FAT_GetTotalBytes(), free = FAT_GetFreeBytes();
        line = AddInfo("Disco");
        AppendNumber(line->Value, sizeof(line->Value), (uint32_t)((total - free) / (1024 * 1024)));
        strlcat(line->Value, "/", sizeof(line->Value));
        AppendNumber(line->Value, sizeof(line->Value), (uint32_t)(total / (1024 * 1024)));
        strlcat(line->Value, " MB FAT", sizeof(line->Value));
        AppendNumber(line->Value, sizeof(line->Value), FAT_GetType());
    }
    else
    {
        line = AddInfo("Disco");
        strlcpy(line->Value, "nessuno", sizeof(line->Value));
    }
}

static uint8_t ArtColor(uint8_t c)
{
    switch (c)
    {
        case 0xB0: return VGA_ATTR(VGA_YELLOW, VGA_BLACK);      // ░ body
        case 0xB1: return VGA_ATTR(VGA_BROWN, VGA_BLACK);       // ▒ beak, feet
        case 0xB2: return VGA_ATTR(VGA_BROWN, VGA_BLACK);       // ▓
        case 0xDB: return VGA_ATTR(VGA_DARK_GRAY, VGA_BLACK);   // █ eye
        default:   return VGA_DEFAULT_COLOR;
    }
}

// prints at most "width" characters of text
static int PrintLimited(const char* text, int width)
{
    int n = 0;
    for (; text[n] && n < width; n++)
        putc(text[n]);
    return n;
}

static void PrintInfoRow(int row)
{
    // row 0: "dacos@DACOS", row 1: underline, then the info lines, then the colors
    const int firstInfo = 2;
    const int colorsRow = firstInfo + g_InfoCount + 1;

    if (row == 0)
    {
        VGA_SetColor(COLOR_TITLE);   printf("dacos");
        VGA_SetColor(VGA_DEFAULT_COLOR); printf("@");
        VGA_SetColor(COLOR_TITLE);   printf("DACOS");
    }
    else if (row == 1)
    {
        VGA_SetColor(VGA_DEFAULT_COLOR);
        printf("-----------");
    }
    else if (row >= firstInfo && row < firstInfo + g_InfoCount)
    {
        InfoLine* line = &g_Info[row - firstInfo];
        VGA_SetColor(COLOR_LABEL);
        int used = PrintLimited(line->Label, INFO_WIDTH);
        used += PrintLimited(": ", INFO_WIDTH - used);
        VGA_SetColor(VGA_DEFAULT_COLOR);
        PrintLimited(line->Value, INFO_WIDTH - used);
    }
    else if (row == colorsRow || row == colorsRow + 1)
    {
        // the 16 VGA colors, like the color bar of neofetch
        int base = (row == colorsRow) ? 0 : 8;
        for (int c = 0; c < 8; c++)
        {
            VGA_SetColor(VGA_ATTR(base + c, VGA_BLACK));
            putc((char)0xDB);
            putc((char)0xDB);
        }
    }
    VGA_SetColor(VGA_DEFAULT_COLOR);
}

void Neofetch_Run()
{
    CollectInfo();

    for (int row = 0; row < ASCII_ART_HEIGHT; row++)
    {
        const uint8_t* art = (const uint8_t*)g_AsciiArt[row];
        int x = 0;
        for (; art[x]; x++)
        {
            VGA_SetColor(ArtColor(art[x]));
            putc((char)art[x]);
        }
        VGA_SetColor(VGA_DEFAULT_COLOR);
        for (; x < INFO_COLUMN; x++)
            putc(' ');

        PrintInfoRow(row);
        putc('\n');
    }
}
