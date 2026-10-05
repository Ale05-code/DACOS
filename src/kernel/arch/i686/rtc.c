#include "rtc.h"
#include "io.h"
#include <stdbool.h>

// The RTC (Real Time Clock) lives in the CMOS chip: write the register number
// to port 0x70, read its value from port 0x71.
#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71

static uint8_t CMOS_Read(uint8_t reg)
{
    i686_outb(CMOS_ADDRESS, reg | 0x80);    // bit 7 = keep NMI disabled while we read
    return i686_inb(CMOS_DATA);
}

static bool RTC_UpdateInProgress()
{
    return (CMOS_Read(0x0A) & 0x80) != 0;
}

static uint8_t BCDToBinary(uint8_t value)
{
    return (value & 0x0F) + (value >> 4) * 10;
}

static void RTC_ReadRaw(uint8_t raw[6])
{
    while (RTC_UpdateInProgress())
        ;
    raw[0] = CMOS_Read(0x00);       // seconds
    raw[1] = CMOS_Read(0x02);       // minutes
    raw[2] = CMOS_Read(0x04);       // hours
    raw[3] = CMOS_Read(0x07);       // day of month
    raw[4] = CMOS_Read(0x08);       // month
    raw[5] = CMOS_Read(0x09);       // year (2 digits)
}

void RTC_Read(DateTime* dt)
{
    // read twice until we get the same values, so we don't catch an update halfway
    uint8_t a[6], b[6];
    RTC_ReadRaw(a);
    for (;;)
    {
        RTC_ReadRaw(b);
        bool same = true;
        for (int i = 0; i < 6; i++)
            if (a[i] != b[i])
                same = false;
        if (same)
            break;
        for (int i = 0; i < 6; i++)
            a[i] = b[i];
    }

    uint8_t statusB = CMOS_Read(0x0B);
    bool binary = (statusB & 0x04) != 0;
    bool hour24 = (statusB & 0x02) != 0;

    uint8_t hour = b[2];
    bool pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!binary)
    {
        for (int i = 0; i < 6; i++)
            if (i != 2)
                b[i] = BCDToBinary(b[i]);
        hour = BCDToBinary(hour);
    }

    if (!hour24)
    {
        if (hour == 12) hour = 0;
        if (pm) hour += 12;
    }

    dt->Second = b[0];
    dt->Minute = b[1];
    dt->Hour = hour;
    dt->Day = b[3];
    dt->Month = b[4];
    dt->Year = 2000 + b[5];
}
