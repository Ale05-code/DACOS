# Capitolo 9: interrupt ed eccezioni (IDT e ISR)

## 1. Cos'è un interrupt

Un **interrupt** è un evento che fa **sospendere** alla CPU quello che sta facendo, eseguire una
funzione apposita (il **gestore**, o **ISR**, *Interrupt Service Routine*) e poi **riprendere**
esattamente da dove era rimasta, come se niente fosse.

Ci sono 256 interrupt possibili (vettori 0–255), di tre tipi:

| Tipo | Chi lo genera | Esempi |
|---|---|---|
| **Eccezioni** (0–31) | la CPU stessa, quando qualcosa va storto | divisione per zero, istruzione non valida, accesso vietato |
| **IRQ hardware** | i dispositivi (capitolo 10) | timer, tastiera, disco |
| **Software** | l'istruzione `int N` | in futuro le syscall (`int 0x80` su Linux) |

Senza gestori, la prima eccezione manda la CPU in **triple fault** e il PC si **riavvia**: il classico
"si riavvia da solo e non capisco perché".

## 2. Dalla IVT alla IDT

- In **real mode** la tabella dei gestori è la **IVT** (*Interrupt Vector Table*) a `0x0000`:
  256 voci da 4 byte (`segmento:offset`). È lì che il BIOS registra i suoi servizi.
- In **protected mode** si usa la **IDT** (*Interrupt Descriptor Table*): può stare ovunque
  (registro `IDTR`, caricato con `lidt`) e ha 256 voci da **8 byte**.

### Una voce della IDT (struttura `IDTEntry`)

```c
typedef struct {
    uint16_t BaseLow;           // indirizzo del gestore, bit 0-15
    uint16_t SegmentSelector;   // segmento di codice: 0x08
    uint8_t  Reserved;          // 0
    uint8_t  Flags;             // tipo + DPL + presente
    uint16_t BaseHigh;          // indirizzo del gestore, bit 16-31
} __attribute__((packed)) IDTEntry;
```

Il byte `Flags`:

| Bit | 7 | 6–5 | 4 | 3–0 |
|---|---|---|---|---|
| | **P** presente | **DPL** (chi può chiamarlo con `int`) | 0 | **tipo** di gate |

Tipi: `0xE` = **interrupt gate a 32 bit** (disattiva gli interrupt mentre gira il gestore), `0xF` =
trap gate (non li disattiva), `0x5` = task gate.

Nel progetto (`idt.c`): `i686_IDT_SetGate(n, gestore, 0x08, RING0 | GATE_32BIT_INT)`, poi
`i686_IDT_EnableGate(n)` accende il bit P.

## 3. Le eccezioni della CPU

| N. | Nome | Codice di errore? | Quando |
|---|---|---|---|
| 0 | Divide Error | no | `div` per zero |
| 1 | Debug | no | single step, breakpoint hardware |
| 2 | NMI | no | errore hardware grave |
| 3 | Breakpoint | no | istruzione `int3` |
| 4 | Overflow | no | `into` |
| 5 | Bound Range Exceeded | no | `bound` |
| 6 | **Invalid Opcode** | no | byte che non sono un'istruzione valida |
| 7 | Device Not Available | no | FPU |
| 8 | **Double Fault** | **sì** (0) | eccezione durante la gestione di un'altra |
| 10 | Invalid TSS | **sì** | |
| 11 | **Segment Not Present** | **sì** | segmento o **gate** con P = 0 |
| 12 | Stack-Segment Fault | **sì** | |
| 13 | **General Protection Fault** | **sì** | quasi tutto ciò che è "vietato" |
| 14 | **Page Fault** | **sì** | (con la paginazione) |
| 16 | x87 FP Exception | no | |
| 17 | Alignment Check | **sì** | |
| 18 | Machine Check | no | |
| 19 | SIMD FP Exception | no | |
| 20 | Virtualization | no | |
| 21 | Control Protection | **sì** | |
| 29 | VMM Communication | **sì** | |
| 30 | Security Exception | **sì** | |

Se un'eccezione avviene mentre la CPU cerca di gestire un double fault → **triple fault** → reset.


## 4. Cosa fa la CPU quando arriva un interrupt

1. (se cambia il livello di privilegio, salva `SS` ed `ESP`);
2. `push EFLAGS`, `push CS`, `push EIP`;
3. per alcune eccezioni, `push` del **codice di errore**;
4. legge la voce N della IDT e salta al gestore.

Il gestore finisce con **`iret`**, che ripristina `EIP`, `CS`, `EFLAGS` (e `ESP`, `SS`).

## 5. I gestori del progetto: uno per ogni vettore

Problema: con 256 vettori, come fa un gestore a sapere **quale** interrupt è arrivato? E alcuni hanno
il codice di errore sullo stack, altri no.

Soluzione: **256 piccoli gestori** generati automaticamente (`isrs_gen.inc`, da
`build_scripts/generate_isrs.sh`), tutti uguali tranne il numero, che "normalizzano" lo stack e poi
saltano a un gestore comune:

```nasm
%macro ISR_NOERRORCODE 1
i686_ISR%1:
    push 0              ; codice di errore finto, così lo stack ha sempre la stessa forma
    push %1             ; numero dell'interrupt
    jmp isr_common
%endmacro

%macro ISR_ERRORCODE 1
i686_ISR%1:
                        ; il codice di errore l'ha già messo la CPU
    push %1
    jmp isr_common
%endmacro
```

```nasm
isr_common:
    pusha               ; salva eax, ecx, edx, ebx, esp, ebp, esi, edi
    xor eax, eax
    mov ax, ds
    push eax            ; salva ds
    mov ax, 0x10        ; segmenti del kernel
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push esp            ; argomento per il C: puntatore a tutto ciò che abbiamo salvato
    call i686_ISR_Handler
    add esp, 4
    pop eax             ; ripristina ds
    mov ds, ax  ...
    popa
    add esp, 8          ; toglie numero e codice di errore
    iret
```

### La struttura `Registers`

Quello che `isr_common` ha messo sullo stack, letto dal basso verso l'alto, è esattamente la
struttura C `Registers` (`isr.h`). L'**ordine dei campi deve corrispondere** alle `push`:

```c
typedef struct {
    uint32_t ds;                                          // push eax (ds)
    uint32_t edi, esi, ebp, useless, ebx, edx, ecx, eax;  // pusha (in ordine inverso)
    uint32_t interrupt, error;                            // push %1, push 0 / dalla CPU
    uint32_t eip, cs, eflags, esp, ss;                    // dalla CPU
} __attribute__((packed)) Registers;
```

(`useless` è l'`ESP` salvato da `pusha`, che non serve.)

## 6. Il gestore in C (`isr.c`)

```c
void i686_ISR_Handler(Registers* regs)
{
    if (g_ISRHandlers[regs->interrupt] != NULL)
        g_ISRHandlers[regs->interrupt](regs);       // qualcuno l'ha registrato: chiamalo
    else if (regs->interrupt >= 32)
        log_err(MODULE, "Unhandled interrupt %d!", regs->interrupt);
    else {
        // eccezione senza gestore: stampa tutto e ferma il sistema
        log_crit(MODULE, "Unhandled exception %d %s", ...);
        ... registri ...
        log_crit(MODULE, "KERNEL PANIC!");
        i686_Panic();                               // cli; hlt
    }
}
```

`i686_ISR_RegisterHandler(n, funzione)` permette agli altri moduli (per esempio gli IRQ) di
"agganciarsi" a un vettore.

## 7. Esempio svolto: `crash_me()`

In `kernel/main.c` c'è `//crash_me();`. `crash_me` (in `io_asm.asm`) esegue `int 0x80`, e
`i686_ISR_Initialize` **disattiva** apposta la gate `0x80`. Togliendo il commento, sulla porta E9
compare (output vero):

```
[ISR] Unhandled exception 11 Segment Not Present
[ISR]   eax=0  ebx=0  ecx=50  edx=3d5  esi=2  edi=36e0
[ISR]   esp=100316  ebp=108bc4  eip=102aea  eflags=206  cs=8  ds=10  ss=0
[ISR]   interrupt=b  errorcode=402
[ISR] KERNEL PANIC!
```

Come si legge:
- **eccezione 11**: la gate 0x80 c'è ma ha **P = 0**, quindi "segmento non presente";
- **codice di errore `0x402`** = `0100 0000 0010` in binario. Per queste eccezioni il formato è:
  bit 0 = EXT (causato da un evento esterno), bit 1 = **IDT** (il problema è nella IDT), bit 2 = TI,
  bit 3–15 = **indice**. Qui: bit 1 = 1 → IDT, indice = `0x402 >> 3` = **0x80**. La CPU ci sta
  dicendo esattamente "la voce 0x80 della IDT non è presente";
- **eip = 0x102aea**: l'istruzione colpevole. Con
  `i686-elf-addr2line -e build/i686_debug/kernel/kernel.elf 0x102aea` (o `nm`) si trova che è dentro
  `crash_me`;
- `esp` e `ss` qui **non hanno senso**: la CPU li salva solo quando cambia il livello di privilegio
  (da ring 3 a ring 0), mentre noi eravamo già in ring 0.

Per provare una divisione per zero, in `crash_me` ci sono le righe commentate
`mov eax, 0 / div eax` → eccezione 0.

## 8. Ordine di inizializzazione (`hal/hal.c`)

```c
VGA_clrscr();
i686_GDT_Initialize();      // segmenti: la IDT usa il selettore 0x08
i686_IDT_Initialize();      // lidt
i686_ISR_Initialize();      // 256 gate, tutte attive tranne 0x80
i686_IRQ_Initialize();      // PIC, poi sti (capitolo 10)
```
