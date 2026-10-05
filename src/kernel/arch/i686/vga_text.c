#include "vga_text.h"
#include <arch/i686/io.h>

#include <stdbool.h>

static uint8_t* const g_ScreenBuffer = (uint8_t*)0xB8000;
static int g_ScreenX = 0, g_ScreenY = 0;
static uint8_t g_Color = VGA_DEFAULT_COLOR;

void VGA_PutCharAt(int x, int y, char c, uint8_t attribute)
{
    if (x < 0 || x >= VGA_WIDTH || y < 0 || y >= VGA_HEIGHT)
        return;
    g_ScreenBuffer[2 * (y * VGA_WIDTH + x)] = (uint8_t)c;
    g_ScreenBuffer[2 * (y * VGA_WIDTH + x) + 1] = attribute;
}

static void VGA_UpdateHardwareCursor()
{
    int pos = g_ScreenY * VGA_WIDTH + g_ScreenX;

    i686_outb(0x3D4, 0x0F);
    i686_outb(0x3D5, (uint8_t)(pos & 0xFF));
    i686_outb(0x3D4, 0x0E);
    i686_outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void VGA_SetCursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= VGA_WIDTH) x = VGA_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= VGA_HEIGHT) y = VGA_HEIGHT - 1;
    g_ScreenX = x;
    g_ScreenY = y;
    VGA_UpdateHardwareCursor();
}

void VGA_GetCursor(int* x, int* y)
{
    *x = g_ScreenX;
    *y = g_ScreenY;
}

void VGA_SetColor(uint8_t attribute)
{
    g_Color = attribute;
}

uint8_t VGA_GetColor()
{
    return g_Color;
}

void VGA_clrscr()
{
    for (int y = 0; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_PutCharAt(x, y, ' ', g_Color);

    VGA_SetCursor(0, 0);
}

static void VGA_scrollback(int lines)
{
    for (int y = lines; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            int from = 2 * (y * VGA_WIDTH + x);
            int to = 2 * ((y - lines) * VGA_WIDTH + x);
            g_ScreenBuffer[to] = g_ScreenBuffer[from];
            g_ScreenBuffer[to + 1] = g_ScreenBuffer[from + 1];
        }

    for (int y = VGA_HEIGHT - lines; y < VGA_HEIGHT; y++)
        for (int x = 0; x < VGA_WIDTH; x++)
            VGA_PutCharAt(x, y, ' ', VGA_DEFAULT_COLOR);

    g_ScreenY -= lines;
}

void VGA_putc(char c)
{
    switch (c)
    {
        case '\n':
            g_ScreenX = 0;
            g_ScreenY++;
            break;

        case '\t':
        {
            int spaces = 4 - (g_ScreenX % 4);
            for (int i = 0; i < spaces; i++)
                VGA_putc(' ');
            return;
        }

        case '\r':
            g_ScreenX = 0;
            break;

        case '\b':
            // move back one cell (also to the end of the previous line)
            if (g_ScreenX > 0)
                g_ScreenX--;
            else if (g_ScreenY > 0)
            {
                g_ScreenY--;
                g_ScreenX = VGA_WIDTH - 1;
            }
            break;

        default:
            VGA_PutCharAt(g_ScreenX, g_ScreenY, c, g_Color);
            g_ScreenX++;
            break;
    }

    if (g_ScreenX >= VGA_WIDTH)
    {
        g_ScreenY++;
        g_ScreenX = 0;
    }
    if (g_ScreenY >= VGA_HEIGHT)
        VGA_scrollback(1);

    VGA_UpdateHardwareCursor();
}
