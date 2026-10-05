#pragma once

// Memory map used by the bootloader (real mode can only reach the first MB)
//
// 0x00000000 - 0x000003FF - interrupt vector table
// 0x00000400 - 0x000004FF - BIOS data area
// 0x00000500 - 0x0000DFFF - stage2 (code + data + bss, checked by linker.ld)
// 0x0000E000 - 0x0000FFF0 - stage2 stack (grows down, also used by the kernel until it sets up its own)
// 0x00007C00 - 0x00007DFF - stage1 (no longer needed once stage2 runs)
// 0x00010000 - 0x0001000F - partition entry copied by stage1
// 0x00020000 - 0x0002FFFF - FAT driver data
// 0x00030000 - 0x0003FFFF - ELF headers
// 0x00040000 - 0x0004FFFF - kernel load buffer
// 0x00050000 - 0x0007FFFF - free
// 0x00080000 - 0x0009FFFF - Extended BIOS data area
// 0x000A0000 - 0x000BFFFF - Video
// 0x000C0000 - 0x000FFFFF - BIOS
// 0x00100000 -            - kernel

#define MEMORY_MIN          0x00000500
#define MEMORY_MAX          0x00080000

#define MEMORY_FAT_ADDR     ((void*)0x20000)
#define MEMORY_FAT_SIZE     0x00010000

#define MEMORY_ELF_ADDR     ((void*)0x30000)
#define MEMORY_ELF_SIZE     0x00010000

#define MEMORY_LOAD_KERNEL  ((void*)0x40000)
#define MEMORY_LOAD_SIZE    0x00010000

#define MEMORY_KERNEL_ADDR  ((void*)0x100000)
