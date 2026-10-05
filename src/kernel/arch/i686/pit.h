#pragma once
#include <stdint.h>

#define PIT_FREQUENCY   100         // timer interrupts per second

void i686_PIT_Initialize();
uint64_t PIT_GetTicks();            // ticks since boot
uint32_t PIT_GetUptimeSeconds();
void PIT_Sleep(uint32_t milliseconds);
