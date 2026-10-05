#pragma once
#include <stdint.h>

typedef struct {
    uint16_t Year;      // e.g. 2026
    uint8_t Month;      // 1-12
    uint8_t Day;        // 1-31
    uint8_t Hour;       // 0-23
    uint8_t Minute;
    uint8_t Second;
} DateTime;

void RTC_Read(DateTime* dt);
