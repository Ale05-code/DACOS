#pragma once
#include "isr.h"

typedef void (*IRQHandler)(Registers* regs);

void i686_IRQ_Initialize();
void i686_IRQ_RegisterHandler(int irq, IRQHandler handler);
void i686_IRQ_Unmask(int irq);      // enable an IRQ line on the PIC
void i686_IRQ_Mask(int irq);        // disable it
