[bits 32]

; Kernel entry point (see ENTRY in linker.ld).
; stage2 calls it as: void entry(BootParams* bootParams)
; The stack we get is the one of stage2, in low memory (below 0x10000):
; we switch to our own stack (64 KB, inside the kernel's .bss), then call start().

extern start
global entry

section .text

entry:
    mov eax, [esp + 4]              ; BootParams* (cdecl: first argument)

    mov esp, kernel_stack_top       ; switch to the kernel stack
    xor ebp, ebp                    ; end of the call frame chain (useful for debuggers)

    push eax
    call start

.halt:
    cli
    hlt
    jmp .halt


section .bss

align 16
kernel_stack_bottom:
    resb 65536                      ; 64 KB
kernel_stack_top:
