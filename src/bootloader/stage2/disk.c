#include "disk.h"
#include "x86.h"
#include "stdio.h"

#define SECTOR_SIZE     512

bool DISK_Initialize(DISK* disk, uint8_t driveNumber)
{
    uint8_t driveType;
    uint16_t cylinders, sectors, heads;

    if (!x86_Disk_GetDriveParams(driveNumber, &driveType, &cylinders, &sectors, &heads))
        return false;

    disk->id = driveNumber;
    disk->haveExtensions = x86_Disk_ExtensionsPresent(driveNumber);
    disk->cylinders = cylinders;
    disk->heads = heads;
    disk->sectors = sectors;

    return true;
}

void DISK_LBA2CHS(DISK* disk, uint32_t lba, uint16_t* cylinderOut, uint16_t* sectorOut, uint16_t* headOut)
{
    // sector = (LBA % sectors per track + 1)
    *sectorOut = lba % disk->sectors + 1;

    // cylinder = (LBA / sectors per track) / heads
    *cylinderOut = (lba / disk->sectors) / disk->heads;

    // head = (LBA / sectors per track) % heads
    *headOut = (lba / disk->sectors) % disk->heads;
}

static bool DISK_ReadSectorsCHS(DISK* disk, uint32_t lba, uint8_t sectors, uint8_t* dataOut)
{
    // read one sector at a time: many BIOSes can't read across a track boundary
    for (uint8_t s = 0; s < sectors; s++)
    {
        uint16_t cylinder, sector, head;
        DISK_LBA2CHS(disk, lba + s, &cylinder, &sector, &head);

        bool ok = false;
        for (int i = 0; i < 3 && !ok; i++)
        {
            ok = x86_Disk_Read(disk->id, cylinder, sector, head, 1, dataOut + s * SECTOR_SIZE);
            if (!ok)
                x86_Disk_Reset(disk->id);
        }

        if (!ok)
            return false;
    }

    return true;
}

bool DISK_ReadSectors(DISK* disk, uint32_t lba, uint8_t sectors, void* dataOut)
{
    if (disk->haveExtensions)
    {
        for (int i = 0; i < 3; i++)
        {
            if (x86_Disk_ExtendedRead(disk->id, lba, sectors, dataOut))
                return true;

            x86_Disk_Reset(disk->id);
        }

        return false;
    }

    return DISK_ReadSectorsCHS(disk, lba, sectors, (uint8_t*)dataOut);
}
