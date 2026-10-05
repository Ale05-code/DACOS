#include "cpu.h"
#include <string.h>

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d)
{
    __asm__ volatile ("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

void CPU_GetBrandString(char* out)
{
    uint32_t a, b, c, d;

    // leaves 0x80000002..4 contain the brand string (48 characters)
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004)
    {
        uint32_t* words = (uint32_t*)out;
        for (uint32_t leaf = 0; leaf < 3; leaf++)
            cpuid(0x80000002 + leaf, &words[leaf * 4], &words[leaf * 4 + 1],
                                     &words[leaf * 4 + 2], &words[leaf * 4 + 3]);
        out[48] = '\0';
    }
    else
    {
        // older CPUs: only the vendor ("GenuineIntel", "AuthenticAMD"...)
        cpuid(0, &a, &b, &c, &d);
        uint32_t* words = (uint32_t*)out;
        words[0] = b;
        words[1] = d;
        words[2] = c;
        out[12] = '\0';
    }

    // remove leading spaces
    char* p = out;
    while (*p == ' ')
        p++;
    memmove(out, p, strlen(p) + 1);
}
