#include <stdio.h>
#include <arch/i686/io.h>

#include <stdarg.h>
#include <stdbool.h>

#include <hal/vfs.h>

void fputc(char c, fd_t file)
{
    VFS_Write(file, (uint8_t*)&c, sizeof(c));
}

void fputs(const char* str, fd_t file)
{
    while(*str)
    {
        fputc(*str, file);
        str++;
    }
}

const char g_HexChars[] = "0123456789abcdef";
const char g_HexCharsUpper[] = "0123456789ABCDEF";

// Writes a number using the given radix, honoring the field width.
// leftAlign: pad on the right; zeroPad: pad with '0' instead of ' '.
static void fprintf_number(fd_t file, unsigned long long number, bool negative, int radix,
                           bool upper, int width, bool leftAlign, bool zeroPad)
{
    const char* digits = upper ? g_HexCharsUpper : g_HexChars;
    char buffer[32];
    int pos = 0;

    // convert number to ASCII (digits come out in reverse order)
    do
    {
        buffer[pos++] = digits[number % radix];
        number /= radix;
    } while (number > 0);

    int len = pos + (negative ? 1 : 0);
    int padding = width > len ? width - len : 0;

    if (!leftAlign && !zeroPad)
        while (padding-- > 0) fputc(' ', file);

    if (negative)
        fputc('-', file);

    if (!leftAlign && zeroPad)
        while (padding-- > 0) fputc('0', file);

    while (--pos >= 0)
        fputc(buffer[pos], file);

    if (leftAlign)
        while (padding-- > 0) fputc(' ', file);
}

static void fprintf_string(fd_t file, const char* str, int width, bool leftAlign)
{
    if (str == NULL)
        str = "(null)";

    int len = 0;
    while (str[len]) len++;
    int padding = width > len ? width - len : 0;

    if (!leftAlign)
        while (padding-- > 0) fputc(' ', file);
    fputs(str, file);
    if (leftAlign)
        while (padding-- > 0) fputc(' ', file);
}

// Supported: %[-][0][width][hh|h|l|ll](c s d i u x X p o %)
void vfprintf(fd_t file, const char* fmt, va_list args)
{
    while (*fmt)
    {
        if (*fmt != '%')
        {
            fputc(*fmt++, file);
            continue;
        }
        fmt++;

        // flags
        bool leftAlign = false, zeroPad = false;
        for (;; fmt++)
        {
            if (*fmt == '-') leftAlign = true;
            else if (*fmt == '0') zeroPad = true;
            else break;
        }

        // width
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        // length: 0 = int, 1 = long, 2 = long long (h/hh are promoted to int anyway)
        int length = 0;
        while (*fmt == 'h') fmt++;
        while (*fmt == 'l') { length++; fmt++; }

        char spec = *fmt;
        if (spec == '\0')
            break;
        fmt++;

        switch (spec)
        {
            case 'c':
                fputc((char)va_arg(args, int), file);
                break;

            case 's':
                fprintf_string(file, va_arg(args, const char*), width, leftAlign);
                break;

            case '%':
                fputc('%', file);
                break;

            case 'd':
            case 'i':
            {
                long long value = length == 2 ? va_arg(args, long long)
                                : length == 1 ? va_arg(args, long)
                                : va_arg(args, int);
                bool negative = value < 0;
                unsigned long long magnitude = negative ? -(unsigned long long)value : (unsigned long long)value;
                fprintf_number(file, magnitude, negative, 10, false, width, leftAlign, zeroPad);
                break;
            }

            case 'u':
            case 'x':
            case 'X':
            case 'p':
            case 'o':
            {
                unsigned long long value = length == 2 ? va_arg(args, unsigned long long)
                                         : length == 1 ? va_arg(args, unsigned long)
                                         : va_arg(args, unsigned int);
                int radix = (spec == 'u') ? 10 : (spec == 'o') ? 8 : 16;
                fprintf_number(file, value, false, radix, spec == 'X', width, leftAlign, zeroPad);
                break;
            }

            default:
                // ignore invalid spec
                break;
        }
    }
}

void fprintf(fd_t file, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);
}

void fprint_buffer(fd_t file, const char* msg, const void* buffer, uint32_t count)
{
    const uint8_t* u8Buffer = (const uint8_t*)buffer;

    fputs(msg, file);
    for (uint32_t i = 0; i < count; i++)
    {
        fputc(g_HexChars[u8Buffer[i] >> 4], file);
        fputc(g_HexChars[u8Buffer[i] & 0xF], file);
    }
    fputs("\n", file);
}

void putc(char c)
{
    fputc(c, VFS_FD_STDOUT);
}

void puts(const char* str)
{
    fputs(str, VFS_FD_STDOUT);
}

void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(VFS_FD_STDOUT, fmt, args);
    va_end(args);
}

void print_buffer(const char* msg, const void* buffer, uint32_t count)
{
    fprint_buffer(VFS_FD_STDOUT, msg, buffer, count);
}

void debugc(char c)
{
    fputc(c, VFS_FD_DEBUG);
}

void debugs(const char* str)
{
    fputs(str, VFS_FD_DEBUG);
}

void debugf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(VFS_FD_DEBUG, fmt, args);
    va_end(args);
}

void debug_buffer(const char* msg, const void* buffer, uint32_t count)
{
    fprint_buffer(VFS_FD_DEBUG, msg, buffer, count);
}
