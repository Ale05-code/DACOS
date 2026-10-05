#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ATA_SECTOR_SIZE     512

// Primary channel, master drive (the disk QEMU/Bochs boot from with -hda / ata0-master)
bool ATA_Initialize();
bool ATA_IsPresent();
uint32_t ATA_GetSectorCount();
const char* ATA_GetModel();

bool ATA_ReadSectors(uint32_t lba, uint8_t count, void* buffer);
bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void* buffer);
bool ATA_Flush();
