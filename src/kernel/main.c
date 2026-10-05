#include <stdint.h>
#include "stdio.h"
#include "memory.h"
#include <hal/hal.h>
#include <debug.h>
#include <boot/bootparams.h>
#include <arch/i686/ata.h>
#include <fs/fat.h>
#include <shell/shell.h>

extern void _init();

void crash_me();

void start(BootParams* bootParams)
{
    // call global constructors
    _init();

    // GDT, IDT, interrupts, timer, keyboard
    HAL_Initialize();

    log_info("Main", "DACOS %s, boot device: %x", DACOS_VERSION, bootParams->BootDevice);
    log_debug("Main", "Memory region count: %d", bootParams->Memory.RegionCount);
    for (int i = 0; i < bootParams->Memory.RegionCount; i++)
    {
        log_debug("Main", "MEM: start=0x%llx length=0x%llx type=%x",
            bootParams->Memory.Regions[i].Begin,
            bootParams->Memory.Regions[i].Length,
            bootParams->Memory.Regions[i].Type);
    }

    // disk and file system
    if (ATA_Initialize())
    {
        log_info("Main", "ATA disk: %s, %u sectors", ATA_GetModel(), ATA_GetSectorCount());
        FAT_Mount();
    }
    else
        log_warn("Main", "no ATA disk: file commands won't work");

    //crash_me();

    Shell_Run(bootParams);
}
