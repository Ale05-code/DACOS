# Capitolo 8: il formato ELF e il caricamento del kernel

## 1. Perché il kernel è un file ELF

Lo stage1 e lo stage2 sono **binari grezzi**: byte da copiare a un indirizzo noto e da eseguire dal
primo. Per il kernel si usa invece **ELF** (*Executable and Linkable Format*), il formato degli
eseguibili Linux, perché:
- dice **dove** caricare ogni pezzo (il kernel va a 1 MB, non a 0);
- dice **qual è l'entry point**;
- distingue la `.bss` (da azzerare) dai dati veri;
- contiene i **simboli di debug**, così GDB sa a quale riga di C corrisponde ogni istruzione;
- è lo standard: gli strumenti (`readelf`, `objdump`, `gdb`) lo capiscono.

## 2. La struttura di un file ELF

```
┌──────────────────────────┐ offset 0
│ ELF header (52 byte)     │ → che file è, entry point, dove sono le tabelle
├──────────────────────────┤ offset 52
│ Program header table     │ → i SEGMENTI: cosa caricare e dove (serve al loader)
├──────────────────────────┤
│ ...                      │
│ contenuto (.text, .data) │
│ ...                      │
├──────────────────────────┤
│ Section header table     │ → le SEZIONI: .text, .data, .symtab… (servono a linker e debugger)
└──────────────────────────┘
```

**Segmenti e sezioni** sono due "viste" dello stesso file. Il loader guarda **solo i segmenti**.

## 3. L'ELF header

Valori reali del nostro `kernel.elf` (`i686-elf-readelf -h build/i686_debug/kernel/kernel.elf`):

| Campo | Valore | Significato |
|---|---|---|
| Magic | `7F 45 4C 46` | `\x7F` `E` `L` `F` |
| Classe | 1 | ELF32 |
| Dati | 1 | little-endian |
| Versione | 1 | |
| Tipo | 2 | EXEC (eseguibile) |
| Macchina | 3 | Intel 80386 |
| **Entry point** | **`0x10ce10`** | l'indirizzo di `entry` (in `kernel/entry.asm`) |
| Inizio program header | 52 | subito dopo l'header |
| Dimensione di un program header | 32 | |
| Numero di program header | 1 | un solo segmento |

In C è la struttura `ELFHeader` di `stage2/elf.h` (con `__attribute__((packed))`, così il
compilatore non aggiunge byte di allineamento tra i campi).

## 4. Il program header

Ogni voce (32 byte, struttura `ELFProgramHeader`):

| Campo | Significato | Nel nostro kernel |
|---|---|---|
| Type | 1 = `PT_LOAD` (da caricare) | LOAD |
| Offset | dove inizia nel file | `0x1000` |
| VirtualAddress | dove va in memoria | `0x100000` |
| PhysicalAddress | (uguale, per noi) | `0x100000` |
| **FileSize** | quanti byte copiare dal file | `0x103b8` (66 488 byte) |
| **MemorySize** | quanti byte occupa in memoria | `0x55950` (350 544 byte) |
| Flags | permessi (R, W, X) | RWE |
| Align | allineamento | `0x1000` |

La differenza `MemorySize − FileSize` = 284 056 byte è la **`.bss`**: variabili globali azzerate,
cioè lo stack del kernel (64 KB), i buffer della shell per i file e per la redirezione (64 KB l'uno),
la cache dei settori, l'elenco di `ls`… Nel file non ci sono, in memoria vanno messe a zero.


## 5. Come lo stage2 carica il kernel (`stage2/elf.c`)

```
ELF_Read(partizione, "/boot/kernel.elf", &entryPoint)
 1. FAT_Open del file (se non esiste → errore, non più crash)
 2. legge 52 byte in 0x30000 (MEMORY_ELF_ADDR)
 3. VALIDA l'header: magic, 32 bit, little-endian, versione, EXEC, x86
 4. salva l'entry point
 5. salta fino al program header e lo legge (sempre in 0x30000)
 6. per ogni segmento PT_LOAD:
      - controlla che sia sopra 1 MB (così non sovrascrive lo stage2 o il BIOS)
      - memset(VirtualAddress, 0, MemorySize)        ← azzera anche la .bss
      - riapre il file e "salta" fino a Offset leggendo a vuoto
      - legge FileSize byte a blocchi da 64 KB nel buffer 0x40000
        e li copia a VirtualAddress
 7. ritorna l'entry point
```

Poi `main.c` dello stage2:

```c
typedef void (*KernelStart)(BootParams* bootParams);
KernelStart kernelEntry;
ELF_Read(&part, "/boot/kernel.elf", (void**)&kernelEntry);
kernelEntry(&g_BootParams);         // salto al kernel: non si torna più
```

### Perché passare da un buffer sotto 1 MB?

Il driver del disco usa il **BIOS**, quindi lavora in real mode e può scrivere **solo nel primo MB**.
Il kernel va a `0x100000`, appena oltre. Quindi: BIOS → buffer a `0x40000` → `memcpy` (in protected
mode, che vede tutta la memoria) → `0x100000`.

### I bug che c'erano qui

1. **La validazione non validava**: c'era `memcmp(magic, ELF_MAGIC, 4) != 0` (vero quando il magic
   è *sbagliato*), e comunque il risultato `ok` non veniva mai controllato. Qualsiasi file veniva
   "eseguito".
2. **File mancante = crash**: se `FAT_Open` restituiva `NULL`, il codice ci leggeva dentro lo stesso.
   Ora stampa `ELF: can't open /boot/kernel.elf` e si ferma (verificato).
3. **Kernel > 64 KB = crash**: per via di `memcpy` a 16 bit (capitolo 6). Verificato con un kernel da
   216 KB: prima andava in crash, ora parte.

## 6. Cosa riceve il kernel: `BootParams`

`src/libs/boot/bootparams.h` è il "contratto" tra bootloader e kernel:

```c
typedef struct {
    uint64_t Begin, Length;
    uint32_t Type;
    uint32_t ACPI;
} MemoryRegion;

typedef struct {
    int RegionCount;
    MemoryRegion* Regions;
} MemoryInfo;

typedef struct {
    MemoryInfo Memory;      // mappa della memoria (capitolo 11)
    uint8_t BootDevice;     // 0x00 floppy, 0x80 disco
} BootParams;
```

Sta in `src/libs/` perché lo includono **sia** lo stage2 **sia** il kernel. Attenzione: `Regions`
punta a un array che sta nella memoria dello stage2, quindi il kernel non deve sovrascrivere quella
zona prima di averlo copiato.

## 7. Comandi utili

```bash
TC=../.toolchains/i686-elf/bin
$TC/i686-elf-readelf -h  build/i686_debug/kernel/kernel.elf   # header
$TC/i686-elf-readelf -l  build/i686_debug/kernel/kernel.elf   # segmenti
$TC/i686-elf-readelf -S  build/i686_debug/kernel/kernel.elf   # sezioni
$TC/i686-elf-objdump -d -M intel build/i686_debug/kernel/kernel.elf | less   # disassembly
$TC/i686-elf-nm build/i686_debug/kernel/kernel.elf | sort      # simboli e indirizzi
```

`kernel-stripped.elf` è la stessa cosa senza simboli di debug (molto più piccolo); nell'immagine
viene copiato quello completo, così GDB e il disco usano lo stesso file.
