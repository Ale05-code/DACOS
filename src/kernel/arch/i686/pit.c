#include "pit.h"
#include "io.h"
#include "irq.h"

// The PIT (Programmable Interval Timer, Intel 8253/8254) counts down from a
// value we choose at 1.193182 MHz; every time it reaches 0 it raises IRQ 0.
#define PIT_BASE_FREQUENCY      1193182
#define PIT_CHANNEL0_PORT       0x40
#define PIT_COMMAND_PORT        0x43

static volatile uint64_t g_Ticks = 0;

static void PIT_IRQHandler(Registers* regs)
{
    g_Ticks++;
}

void i686_PIT_Initialize()
{
    uint32_t divisor = PIT_BASE_FREQUENCY / PIT_FREQUENCY;

    // channel 0, access mode lobyte/hibyte, mode 3 (square wave), binary
    i686_outb(PIT_COMMAND_PORT, 0x36);
    i686_outb(PIT_CHANNEL0_PORT, divisor & 0xFF);
    i686_outb(PIT_CHANNEL0_PORT, (divisor >> 8) & 0xFF);

    i686_IRQ_RegisterHandler(0, PIT_IRQHandler);
    i686_IRQ_Unmask(0);
}

uint64_t PIT_GetTicks()
{
    return g_Ticks;
}

uint32_t PIT_GetUptimeSeconds()
{
    return (uint32_t)(g_Ticks / PIT_FREQUENCY);
}

void PIT_Sleep(uint32_t milliseconds)
{
    uint64_t end = g_Ticks + (milliseconds * PIT_FREQUENCY + 999) / 1000;
    while (g_Ticks < end)
        i686_Halt();
}
