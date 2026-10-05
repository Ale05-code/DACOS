#include "ata.h"
#include "io.h"
#include <string.h>

// ATA (IDE) disk driver, PIO mode, 28-bit LBA, primary channel master drive.
// PIO = the CPU moves every word of data itself through the data port.
// We poll the status register instead of waiting for IRQ 14: simple and enough here.

#define ATA_DATA            0x1F0   // 16-bit data port
#define ATA_ERROR           0x1F1
#define ATA_SECTOR_COUNT    0x1F2
#define ATA_LBA_LOW         0x1F3
#define ATA_LBA_MID         0x1F4
#define ATA_LBA_HIGH        0x1F5
#define ATA_DRIVE_HEAD      0x1F6   // bit 6 = LBA mode, bit 4 = drive (0 = master), bits 0-3 = LBA 24-27
#define ATA_STATUS          0x1F7   // read
#define ATA_COMMAND         0x1F7   // write
#define ATA_ALT_STATUS      0x3F6   // reading it doesn't clear pending interrupts

#define ATA_SR_ERR          0x01    // error
#define ATA_SR_DRQ          0x08    // data request: ready to transfer data
#define ATA_SR_DF           0x20    // drive fault
#define ATA_SR_BSY          0x80    // busy

#define ATA_CMD_READ        0x20
#define ATA_CMD_WRITE       0x30
#define ATA_CMD_FLUSH       0xE7
#define ATA_CMD_IDENTIFY    0xEC

static bool g_Present = false;
static uint32_t g_SectorCount = 0;
static char g_Model[41];

// ~400 ns pause: reading the alternate status 4 times
static void ATA_Delay()
{
    for (int i = 0; i < 4; i++)
        i686_inb(ATA_ALT_STATUS);
}

static bool ATA_WaitNotBusy()
{
    for (uint32_t i = 0; i < 10000000; i++)
    {
        if ((i686_inb(ATA_STATUS) & ATA_SR_BSY) == 0)
            return true;
    }
    return false;       // timeout
}

static bool ATA_WaitDataRequest()
{
    for (uint32_t i = 0; i < 10000000; i++)
    {
        uint8_t status = i686_inb(ATA_STATUS);
        if (status & (ATA_SR_ERR | ATA_SR_DF))
            return false;
        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRQ))
            return true;
    }
    return false;
}

static bool ATA_SetupLBA(uint32_t lba, uint8_t count)
{
    if (!ATA_WaitNotBusy())
        return false;

    i686_outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ATA_Delay();
    i686_outb(ATA_SECTOR_COUNT, count);
    i686_outb(ATA_LBA_LOW, lba & 0xFF);
    i686_outb(ATA_LBA_MID, (lba >> 8) & 0xFF);
    i686_outb(ATA_LBA_HIGH, (lba >> 16) & 0xFF);
    return true;
}

bool ATA_Initialize()
{
    g_Present = false;

    // no controller at all: the bus "floats" and reads 0xFF
    if (i686_inb(ATA_STATUS) == 0xFF)
        return false;

    i686_outb(ATA_DRIVE_HEAD, 0xA0);        // select master
    ATA_Delay();
    i686_outb(ATA_SECTOR_COUNT, 0);
    i686_outb(ATA_LBA_LOW, 0);
    i686_outb(ATA_LBA_MID, 0);
    i686_outb(ATA_LBA_HIGH, 0);
    i686_outb(ATA_COMMAND, ATA_CMD_IDENTIFY);
    ATA_Delay();

    if (i686_inb(ATA_STATUS) == 0)          // no drive
        return false;
    if (!ATA_WaitNotBusy())
        return false;

    // ATAPI (CD-ROM) and SATA devices set these registers: not a plain ATA disk
    if (i686_inb(ATA_LBA_MID) != 0 || i686_inb(ATA_LBA_HIGH) != 0)
        return false;

    if (!ATA_WaitDataRequest())
        return false;

    uint16_t identify[256];
    i686_insw(ATA_DATA, identify, 256);

    // words 60-61: number of sectors addressable with 28-bit LBA
    g_SectorCount = identify[60] | ((uint32_t)identify[61] << 16);

    // words 27-46: model name, two characters per word, swapped
    for (int i = 0; i < 20; i++)
    {
        g_Model[2 * i] = identify[27 + i] >> 8;
        g_Model[2 * i + 1] = identify[27 + i] & 0xFF;
    }
    g_Model[40] = '\0';
    for (int i = 39; i >= 0 && g_Model[i] == ' '; i--)
        g_Model[i] = '\0';

    g_Present = true;
    return true;
}

bool ATA_IsPresent()                { return g_Present; }
uint32_t ATA_GetSectorCount()       { return g_SectorCount; }
const char* ATA_GetModel()          { return g_Model; }

bool ATA_ReadSectors(uint32_t lba, uint8_t count, void* buffer)
{
    if (!g_Present || count == 0)
        return false;

    if (!ATA_SetupLBA(lba, count))
        return false;
    i686_outb(ATA_COMMAND, ATA_CMD_READ);

    uint8_t* out = (uint8_t*)buffer;
    for (int s = 0; s < count; s++)
    {
        ATA_Delay();
        if (!ATA_WaitDataRequest())
            return false;
        i686_insw(ATA_DATA, out + s * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE / 2);
    }
    return true;
}

bool ATA_WriteSectors(uint32_t lba, uint8_t count, const void* buffer)
{
    if (!g_Present || count == 0)
        return false;

    if (!ATA_SetupLBA(lba, count))
        return false;
    i686_outb(ATA_COMMAND, ATA_CMD_WRITE);

    const uint8_t* in = (const uint8_t*)buffer;
    for (int s = 0; s < count; s++)
    {
        ATA_Delay();
        if (!ATA_WaitDataRequest())
            return false;
        i686_outsw(ATA_DATA, in + s * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE / 2);
    }

    ATA_Delay();
    if (!ATA_WaitNotBusy())
        return false;
    return (i686_inb(ATA_STATUS) & (ATA_SR_ERR | ATA_SR_DF)) == 0;
}

bool ATA_Flush()
{
    if (!g_Present)
        return false;
    if (!ATA_WaitNotBusy())
        return false;
    i686_outb(ATA_DRIVE_HEAD, 0xE0);
    i686_outb(ATA_COMMAND, ATA_CMD_FLUSH);
    ATA_Delay();
    return ATA_WaitNotBusy();
}
