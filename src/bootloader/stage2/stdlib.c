#include "stdlib.h"
#include <stdint.h>

static void swap_bytes(uint8_t* a, uint8_t* b, size_t size)
{
    for (size_t k = 0; k < size; k++)
    {
        uint8_t temp = a[k];
        a[k] = b[k];
        b[k] = temp;
    }
}

// Insertion sort: simple and correct, and the arrays we sort (LFN blocks) are tiny.
// Same interface as the standard qsort.
void qsort(void* base,
           size_t num,
           size_t size,
           int (*compar)(const void*, const void*))
{
    uint8_t* bytes = (uint8_t*)base;

    for (size_t i = 1; i < num; i++)
    {
        for (size_t j = i; j > 0 && compar(bytes + (j - 1) * size, bytes + j * size) > 0; j--)
            swap_bytes(bytes + (j - 1) * size, bytes + j * size, size);
    }
}
