
global i686_outb
i686_outb:
    [bits 32]
    mov dx, [esp + 4]
    mov al, [esp + 8]
    out dx, al
    ret

global i686_inb
i686_inb:
    [bits 32]
    mov dx, [esp + 4]
    xor eax, eax
    in al, dx
    ret

global i686_Panic
i686_Panic:
    cli
    hlt

global i686_EnableInterrupts
i686_EnableInterrupts:
    sti
    ret

global i686_DisableInterrupts
i686_DisableInterrupts:
    cli
    ret

global crash_me
crash_me:
    ; div by 0
    ; mov ecx, 0x1337
    ; mov eax, 0
    ; div eax
    int 0x80
    ret

; 16-bit port I/O (used by the ATA driver)

global i686_outw
i686_outw:
    [bits 32]
    mov dx, [esp + 4]
    mov ax, [esp + 8]
    out dx, ax
    ret

global i686_inw
i686_inw:
    [bits 32]
    mov dx, [esp + 4]
    xor eax, eax
    in ax, dx
    ret

; void i686_insw(uint16_t port, void* buffer, uint32_t count): reads count words
global i686_insw
i686_insw:
    [bits 32]
    push edi
    mov dx, [esp + 8]
    mov edi, [esp + 12]
    mov ecx, [esp + 16]
    cld
    rep insw
    pop edi
    ret

; void i686_outsw(uint16_t port, const void* buffer, uint32_t count): writes count words
global i686_outsw
i686_outsw:
    [bits 32]
    push esi
    mov dx, [esp + 8]
    mov esi, [esp + 12]
    mov ecx, [esp + 16]
    cld
.loop:
    outsw                   ; one word at a time with a short pause: some
    jmp $+2                 ; ATA controllers don't like back-to-back writes
    loop .loop
    pop esi
    ret

; void i686_Halt(): waits for the next interrupt
global i686_Halt
i686_Halt:
    sti
    hlt
    ret
