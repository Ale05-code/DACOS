#pragma once
#include <stdint.h>

#define VGA_WIDTH       80
#define VGA_HEIGHT      25

// VGA colors (4 bits). An attribute is (background << 4) | foreground.
enum {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN, VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GRAY,
    VGA_DARK_GRAY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN, VGA_LIGHT_RED,
    VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

#define VGA_ATTR(fg, bg)    ((uint8_t)(((bg) << 4) | (fg)))
#define VGA_DEFAULT_COLOR   VGA_ATTR(VGA_LIGHT_GRAY, VGA_BLACK)

void VGA_clrscr();
void VGA_putc(char c);              // handles \n \r \t \b, wraps and scrolls

void VGA_SetColor(uint8_t attribute);   // color used by the next VGA_putc
uint8_t VGA_GetColor();

void VGA_SetCursor(int x, int y);
void VGA_GetCursor(int* x, int* y);

// direct access to a screen cell (doesn't move the cursor)
void VGA_PutCharAt(int x, int y, char c, uint8_t attribute);
