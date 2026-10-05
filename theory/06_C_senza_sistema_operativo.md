# Capitolo 6: programmare in C senza sistema operativo

## 1. Perché passare al C

L'assembly va bene per poche centinaia di istruzioni. Un driver FAT, un parser ELF o `printf`
scritti in assembly sarebbero enormi e pieni di errori. Dallo stage2 in poi si usa il **C** (e, nella
libreria `libs/core`, un po' di **C++**).

Ma il C "normale" dà per scontate molte cose che noi non abbiamo.

## 2. Ambiente hosted e freestanding

- **Hosted**: il programma gira sopra un sistema operativo. C'è la libreria standard (`printf`,
  `malloc`, `fopen`…), `main` viene chiamato dal sistema, la memoria è già organizzata.
- **Freestanding**: non c'è niente. È il nostro caso. Si compila con:
  - `-ffreestanding`: il compilatore non assume che le funzioni standard esistano né cosa facciano;
  - `-nostdlib`: il linker non aggiunge la libreria C né il codice di avvio (`crt0`).

Restano disponibili solo gli **header freestanding** forniti dal compilatore, che contengono solo
tipi e macro: `<stdint.h>` (`uint8_t`, `uint32_t`…), `<stddef.h>` (`size_t`, `NULL`),
`<stdbool.h>`, `<stdarg.h>` (`va_list`, per le funzioni con argomenti variabili).

**Tutto il resto lo scriviamo noi**: `memcpy`, `memset`, `memcmp` (`memory.c`), `strlen`,
`strchr` (`string.c`), `toupper` (`ctype.c`), `qsort` (`stdlib.c`), `printf` (`stdio.c`).


## 3. Il cross-compiler (`i686-elf-gcc`)

Vedi anche il capitolo 0. Lo costruiamo con `scripts/setup_toolchain.sh`, che:

1. scarica **binutils 2.37** e lo compila con
   `--target=i686-elf --prefix=.../i686-elf --with-sysroot --disable-nls --disable-werror`;
2. scarica **GCC 11.2.0** e lo compila con `--target=i686-elf --enable-languages=c,c++
   --without-headers`, ma solo `all-gcc` e `all-target-libgcc` (niente libreria C, che non esiste).

### libgcc

Una CPU a 32 bit non ha un'istruzione per dividere numeri a **64 bit**. Quando in `printf` scriviamo

```c
unsigned long long rem = number % radix;
```

GCC genera una chiamata a `__umoddi3`, che sta in **libgcc**. Per questo il progetto linka sempre
con `-lgcc` (`LIBS = ['gcc']` in `SConstruct`).

## 4. La convenzione di chiamata cdecl

Per chiamare funzioni assembly dal C (e viceversa) bisogna sapere **dove stanno gli argomenti**. Su
i686 la convenzione standard è **cdecl**:

- gli argomenti vanno sullo **stack**, da **destra a sinistra** (il primo argomento è il più vicino
  alla cima);
- ogni argomento occupa almeno **4 byte** (anche un `uint8_t`);
- il valore di ritorno va in **`EAX`** (per 64 bit in `EDX:EAX`);
- `EAX`, `ECX`, `EDX` li può sporcare chi viene chiamato; `EBX`, `ESI`, `EDI`, `EBP` vanno
  **preservati**;
- è il **chiamante** che ripulisce lo stack.

Esempio: `x86_Disk_Read(drive, cylinder, sector, head, count, buffer)`. Dopo il prologo:

```nasm
push ebp
mov ebp, esp
```

lo stack è:

| Indirizzo | Contenuto |
|---|---|
| `[ebp + 28]` | `buffer` |
| `[ebp + 24]` | `count` |
| `[ebp + 20]` | `head` |
| `[ebp + 16]` | `sector` |
| `[ebp + 12]` | `cylinder` |
| `[ebp + 8]` | `drive` (primo argomento) |
| `[ebp + 4]` | indirizzo di ritorno |
| `[ebp + 0]` | vecchio `EBP` |


In C si dichiara così:

```c
#define ASMCALL __attribute__((cdecl))
bool ASMCALL x86_Disk_Read(uint8_t drive, uint16_t cylinder, ...);
```

e in assembly la funzione va resa visibile con `global x86_Disk_Read`.

## 5. Il linker e i linker script

Il compilatore produce **file oggetto** (`.o`) con il codice diviso in **sezioni**:

| Sezione | Contenuto |
|---|---|
| `.text` | codice |
| `.rodata` | costanti (stringhe, tabelle) |
| `.data` | variabili globali inizializzate |
| `.bss` | variabili globali **non** inizializzate (o a zero): nel file non occupano spazio |

Il **linker** unisce gli oggetti e decide **a quale indirizzo** andrà ogni sezione. In un programma
Linux lo decide da solo; noi glielo diciamo con un **linker script**. Quello dello stage2:

```
ENTRY(entry)
OUTPUT_FORMAT("binary")         /* byte grezzi, niente header */
phys = 0x00000500;              /* lo stage1 ci carica qui */

SECTIONS
{
    . = phys;                   /* "." = indirizzo corrente */
    .entry  : { __entry_start = .;  *(.entry)  }   /* deve essere il primo byte! */
    .text   : { __text_start = .;   *(.text)   }
    .data   : { __data_start = .;   *(.data)   }
    .rodata : { __rodata_start = .; *(.rodata) }
    .bss    : { __bss_start = .;    *(.bss)    }
    __end = .;
}
ASSERT(__end <= 0xE000, "stage2 is too big: it would overlap its stack")
```

- `OUTPUT_FORMAT("binary")`: lo stage1 salta al **primo byte** del file, quindi serve un file "nudo"
  in cui il primo byte sia la prima istruzione di `entry`.
- I simboli come `__bss_start` ed `__end` sono **variabili del linker**, usate dal codice assembly
  per azzerare la `.bss`.
- L'`ASSERT` l'ho aggiunto io: se lo stage2 cresce troppo e invade il suo stack, la compilazione
  fallisce invece di produrre un sistema che si blocca in modo misterioso.
- Il kernel invece usa `OUTPUT_FORMAT("elf32-i386")` e `. = 0x00100000`: è un file ELF vero, che lo
  stage2 sa leggere (capitolo 8).

### Il file `.map`

Con `-Wl,-Map=stage2.map` il linker scrive dove ha messo ogni simbolo. Utilissimo per il debug.
Valori reali dello stage2 attuale:

```
__bss_start = 0x3680
__end       = 0x4eec      → stage2 occupa 0x500–0x4eec (~19 KB con la .bss)
```

## 6. Azzerare la `.bss`

Lo standard C garantisce che le variabili globali senza inizializzatore valgano 0. Siccome la `.bss`
non è nel file, qualcuno deve azzerarla:

- **stage2**: lo fa `entry.asm`:

  ```nasm
  mov edi, __bss_start
  mov ecx, __end
  sub ecx, edi            ; ecx = dimensione
  mov al, 0
  cld                     ; direzione: in avanti
  rep stosb               ; ripeti ecx volte: [es:edi] = al, edi++
  ```

- **kernel**: lo fa il **loader ELF**, che azzera `MemorySize` byte prima di copiare `FileSize`
  byte (la differenza è proprio la `.bss`).

## 7. I costruttori globali: crti, crtbegin, crtend, crtn

In C++ (e in C con `__attribute__((constructor))`) ci sono funzioni da eseguire **prima** del codice
principale (i costruttori degli oggetti globali). GCC le raccoglie nella sezione `.init`, ma vuole
che i pezzi siano linkati in un ordine preciso:

```
crti.o   crtbegin.o   [i nostri oggetti]   crtend.o   crtn.o
```

- `crti.asm` (nostro): l'inizio della funzione `_init` (`push ebp; mov ebp, esp`);
- `crtbegin.o` / `crtend.o` (di GCC, in `lib/gcc/i686-elf/11.2.0/`): il codice che chiama i
  costruttori;
- `crtn.asm` (nostro): la fine (`pop ebp; ret`).

Per questo gli `SConscript` estraggono `crti.o` e `crtn.o` dalla lista e li mettono in testa e in
coda. Poi `entry.asm` dello stage2 e `start()` del kernel chiamano `_init()`.

## 8. Lo stack del kernel

Prima il kernel usava lo stack dello stage2 (a `0xFFF0`, in memoria bassa). Ho aggiunto
`src/kernel/entry.asm`, che è il nuovo punto di ingresso del kernel (`ENTRY(entry)` in `linker.ld`):

```nasm
entry:
    mov eax, [esp + 4]          ; BootParams* passato dallo stage2
    mov esp, kernel_stack_top   ; stack nostro: 16 KB nella .bss del kernel
    xor ebp, ebp                ; fine della catena dei frame (per il debugger)
    push eax
    call start
section .bss
    resb 16384
kernel_stack_top:
```

Ora il kernel non dipende più da memoria che "appartiene" al bootloader. Si vede anche in GDB:
`bt` mostra `start` chiamato da `entry`.

## 9. Variabili, puntatori e indirizzi fissi

In un sistema operativo è normale scrivere cose che in un programma normale sarebbero follie:

```c
uint8_t* g_ScreenBuffer = (uint8_t*)0xB8000;     // memoria video
FAT_Data* g_Data = (FAT_Data*)0x20000;          // zona scelta da noi (memdefs.h)
```

Funziona perché in protected mode con il flat model **un puntatore è un indirizzo fisico**.
Bisogna però tenere una **mappa della memoria** (`stage2/memdefs.h`) per non sovrapporre le cose.
