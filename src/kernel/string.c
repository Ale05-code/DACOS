#include "string.h"
#include <stdint.h>

size_t strlen(const char* str)
{
    size_t len = 0;
    while (str[len])
        len++;
    return len;
}

int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        if (a[i] != b[i] || a[i] == '\0')
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

int strcasecmp(const char* a, const char* b)
{
    while (*a && tolower(*a) == tolower(*b))
    {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char* strchr(const char* str, int c)
{
    for (; *str; str++)
        if (*str == (char)c)
            return (char*)str;
    return c == '\0' ? (char*)str : NULL;
}

char* strrchr(const char* str, int c)
{
    const char* last = NULL;
    for (; *str; str++)
        if (*str == (char)c)
            last = str;
    return c == '\0' ? (char*)str : (char*)last;
}

size_t strlcpy(char* dst, const char* src, size_t size)
{
    size_t len = strlen(src);
    if (size > 0)
    {
        size_t n = len < size - 1 ? len : size - 1;
        for (size_t i = 0; i < n; i++)
            dst[i] = src[i];
        dst[n] = '\0';
    }
    return len;
}

size_t strlcat(char* dst, const char* src, size_t size)
{
    size_t dstLen = 0;
    while (dstLen < size && dst[dstLen])
        dstLen++;
    if (dstLen == size)
        return size + strlen(src);
    return dstLen + strlcpy(dst + dstLen, src, size - dstLen);
}

void* memmove(void* dst, const void* src, size_t num)
{
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;

    if (d < s)
        for (size_t i = 0; i < num; i++)
            d[i] = s[i];
    else
        for (size_t i = num; i > 0; i--)
            d[i - 1] = s[i - 1];

    return dst;
}

int toupper(int c)  { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }
int tolower(int c)  { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }
bool isdigit(int c) { return c >= '0' && c <= '9'; }
bool isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isalnum(int c) { return isalpha(c) || isdigit(c); }
bool isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

int atoi(const char* str)
{
    int sign = 1, value = 0;
    while (isspace(*str))
        str++;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    while (isdigit(*str))
        value = value * 10 + (*str++ - '0');
    return sign * value;
}
