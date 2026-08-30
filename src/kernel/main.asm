org 0x7C00      ; ORG directive: tells assembler where we expect our code to be loaded
bits 16         ; BITS directive: tell assembler to emit 16-bit code

%define ENDL 0x0D, 0x0A


start:
    jmp main


;
; print a string to the screen
; Params:
;   - ds:si points to string
;
puts:
    ; save registers we will modify
    push si
    push ax

.loop:
    lodsb           ; loads next character in al
    or al, al       ; verify if next character is null
    jz .done        ; jump if zero flag is set

    mov ah, 0x0e    ; call bios interrupt
    int 0x10

    jmp .loop

.done:
    pop ax
    pop si
    ret

main:
    ; setup data segment
    mov ax, 0       ; can't write to ds/es directly
    mov ds, ax
    mov es, ax

    ; setup stack
    mov ss, ax
    mov sp, 0x7C00  ; stack grows downwards from where we are loaded in memory

    ; print message
    mov si, msg_hello
    call puts

    hlt             ; HLT stops cpu from executing (it can be resumed by an interrupt)

.halt:
    jmp .halt       ; JMP location, jumps to given location, unconditionally


msg_hello: db 'Hello world !', ENDL, 0


; SIGNATURE
times 510-($-$$) db 0
dw 0AA55h