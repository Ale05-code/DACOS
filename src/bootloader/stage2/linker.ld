ENTRY(entry)
OUTPUT_FORMAT("binary")
phys = 0x00000500;

SECTIONS
{
    . = phys;

    .entry              : { __entry_start = .;      *(.entry)   }
    .text               : { __text_start = .;       *(.text)    }
    .data               : { __data_start = .;       *(.data)    }
    .rodata             : { __rodata_start = .;     *(.rodata)  }
    .bss                : { __bss_start = .;        *(.bss)     }
    
    __end = .;
}

/* stage2 must not grow into its own stack (sp = 0xFFF0) */
ASSERT(__end <= 0xE000, "stage2 is too big: it would overlap its stack")
