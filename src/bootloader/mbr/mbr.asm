;
; Master Boot Record (sector 0 of the "disk" image)
;
; The BIOS loads us at 0000:7C00 with DL = boot drive. We:
;   1. move ourselves to 0000:0600, so the VBR can be loaded at 0x7C00
;   2. look for the active partition in the partition table
;   3. load its first sector (the VBR = our stage1) at 0000:7C00,
;      using LBA (int 13h/42h) if available, CHS (int 13h/02h) otherwise
;   4. jump to it with DL = boot drive and DS:SI -> partition entry
;
; Only the first 440 bytes are written to the disk: the disk signature,
; the partition table and the 0xAA55 signature are left as they are.
;

bits 16
org 0x0600

%define ENDL 0x0D, 0x0A

RELOCATED_ADDR          equ 0x0600
LOAD_ADDR               equ 0x7C00
PARTITION_TABLE         equ RELOCATED_ADDR + 0x1BE

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, LOAD_ADDR
    sti

    ; relocate to 0x0600
    mov si, LOAD_ADDR
    mov di, RELOCATED_ADDR
    mov cx, 256                     ; 256 words = 512 bytes
    cld
    rep movsw
    jmp 0:relocated

relocated:
    mov [drive], dl

    ; find the active partition (bit 7 of the first byte)
    mov si, PARTITION_TABLE
    mov cx, 4
.find:
    test byte [si], 0x80
    jnz .found
    add si, 16
    loop .find

    mov si, msg_no_active
    jmp error

.found:
    ; are the LBA extensions available?
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    stc
    int 13h
    jc .read_chs
    cmp bx, 0xAA55
    jne .read_chs

    ; LBA read: fill the disk address packet with the partition start
    mov eax, [si + 8]
    mov [dap.lba], eax
    push si
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    stc
    int 13h
    pop si
    jnc .loaded

.read_chs:
    ; CHS read, using the start address stored in the partition entry
    mov ax, 0x0201                  ; ah = 02 (read), al = 1 sector
    mov bx, LOAD_ADDR               ; es:bx = 0000:7C00
    mov dl, [drive]
    mov dh, [si + 1]                ; head
    mov cx, [si + 2]                ; sector (bits 0-5) + cylinder
    stc
    int 13h
    jnc .loaded

    mov si, msg_read_failed
    jmp error

.loaded:
    cmp word [LOAD_ADDR + 510], 0xAA55
    je .boot

    mov si, msg_not_bootable
    jmp error

.boot:
    mov dl, [drive]                 ; boot drive
                                    ; ds:si -> partition entry
    jmp 0:LOAD_ADDR


; prints the string at ds:si, then halts
error:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp error
.halt:
    cli
    hlt
    jmp .halt


drive:              db 0

dap:
    .size:          db 10h
                    db 0
    .count:         dw 1
    .offset:        dw LOAD_ADDR
    .segment:       dw 0
    .lba:           dq 0

msg_no_active:      db 'MBR: no active partition', ENDL, 0
msg_read_failed:    db 'MBR: read error', ENDL, 0
msg_not_bootable:   db 'MBR: partition not bootable', ENDL, 0

; the code must fit before the disk signature (offset 440)
%if ($ - $$) > 440
    %error "MBR code is too big"
%endif
times 440 - ($ - $$) db 0
