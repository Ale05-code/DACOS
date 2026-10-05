#pragma once
#include <stdint.h>

// Fills "out" (at least 49 bytes) with the CPU name, e.g. "QEMU Virtual CPU version 2.5+"
void CPU_GetBrandString(char* out);
