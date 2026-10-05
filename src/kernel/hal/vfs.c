#include "vfs.h"
#include <arch/i686/vga_text.h>
#include <arch/i686/e9.h>

// code page 437 (the VGA font) → Unicode, for the bytes 0x80-0xFF
static const uint16_t g_CP437ToUnicode[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

// The debug port goes to a terminal that uses UTF-8: convert the characters of
// code page 437 (accents, box drawing, shades...) so they show up correctly there.
static void MirrorToDebug(uint8_t c)
{
    if (c < 0x80)
    {
        e9_putc(c);
        return;
    }
    uint16_t u = g_CP437ToUnicode[c - 0x80];
    if (u < 0x800)
    {
        e9_putc(0xC0 | (u >> 6));
        e9_putc(0x80 | (u & 0x3F));
    }
    else
    {
        e9_putc(0xE0 | (u >> 12));
        e9_putc(0x80 | ((u >> 6) & 0x3F));
        e9_putc(0x80 | (u & 0x3F));
    }
}

static char* g_CaptureBuffer = NULL;
static size_t g_CaptureCapacity = 0;
static size_t g_CaptureLength = 0;
static bool g_CaptureOverflow = false;
static bool g_MirrorStdout = true;

void VFS_StartCapture(char* buffer, size_t capacity)
{
    g_CaptureBuffer = buffer;
    g_CaptureCapacity = capacity;
    g_CaptureLength = 0;
    g_CaptureOverflow = false;
}

size_t VFS_StopCapture()
{
    g_CaptureBuffer = NULL;
    return g_CaptureLength;
}

bool VFS_CaptureOverflow()
{
    return g_CaptureOverflow;
}

void VFS_DebugPutChar(uint8_t c)
{
    MirrorToDebug(c);
}

void VFS_SetStdoutMirror(bool enabled)
{
    g_MirrorStdout = enabled;
}

int VFS_Write(fd_t file, uint8_t* data, size_t size)
{
    switch (file)
    {
    case VFS_FD_STDIN:
        return 0;

    case VFS_FD_STDOUT:
        if (g_CaptureBuffer != NULL)
        {
            for (size_t i = 0; i < size; i++)
            {
                if (g_CaptureLength < g_CaptureCapacity)
                    g_CaptureBuffer[g_CaptureLength++] = data[i];
                else
                    g_CaptureOverflow = true;
            }
            return size;
        }
        // fall through

    case VFS_FD_STDERR:
        for (size_t i = 0; i < size; i++)
        {
            VGA_putc(data[i]);
            if (g_MirrorStdout)
                MirrorToDebug(data[i]);
        }
        return size;

    case VFS_FD_DEBUG:
        for (size_t i = 0; i < size; i++)
            e9_putc(data[i]);
        return size;

    default:
        return -1;
    }
}
