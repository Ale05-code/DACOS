#include "power.h"
#include "io.h"
#include <stdint.h>

void Power_Reboot()
{
    i686_DisableInterrupts();

    // 1. ask the keyboard controller (8042) to pulse the CPU reset line
    for (int i = 0; i < 100000 && (i686_inb(0x64) & 0x02); i++)
        ;
    i686_outb(0x64, 0xFE);

    // 2. if that didn't work: load an empty IDT and cause an interrupt.
    //    No handler → double fault → triple fault → the CPU resets.
    struct { uint16_t limit; uint32_t base; } __attribute__((packed)) emptyIdt = { 0, 0 };
    __asm__ volatile ("lidt %0; int $3" : : "m"(emptyIdt));

    for (;;)
        __asm__ volatile ("hlt");
}

void Power_Shutdown()
{
    // A real OS reads the ACPI tables to find how to turn off the machine.
    // Emulators have fixed ports that do the same thing:
    i686_outw(0x604, 0x2000);       // QEMU (newer versions, also with -machine q35)
    i686_outw(0xB004, 0x2000);      // Bochs and older QEMU
    i686_outw(0x4004, 0x3400);      // VirtualBox
    // still here: none of them worked
}
