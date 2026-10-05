# Capitolo 0: gli strumenti

Prima di scrivere anche una sola riga di sistema operativo serve una "cassetta degli attrezzi".
Qui c'è cosa fa ogni strumento e perché serve proprio quello.

## 1. Cosa serve, in breve

| Strumento | Pacchetto Ubuntu | A cosa serve |
|---|---|---|
| Editor di testo | (qualsiasi: micro, VS Code…) | scrivere il codice |
| **NASM** | `nasm` | assembler: trasforma l'assembly x86 in codice macchina |
| **SCons** | `scons` | sistema di build (al posto di `make`), scritto in Python |
| **cross-compiler i686-elf** | si compila da sorgente | compilatore C/C++ per il nostro OS (non per Linux) |
| **mkfs.fat** | `dosfstools` | crea il file system FAT dentro l'immagine |
| **mtools** | `mtools` | copia file dentro un'immagine FAT senza montarla |
| **pyparted** | `python3-parted` | crea la tabella delle partizioni (MBR) da Python |
| **QEMU** | `qemu-system-x86` | emulatore: è il "PC finto" su cui proviamo il sistema |
| **Bochs** | `bochs bochs-sdl bochsbios vgabios` | emulatore lento ma con un debugger potentissimo |
| **GDB** | `gdb` | debugger, si collega a QEMU |
| strumenti per compilare GCC | `build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo wget` | servono **solo** per costruire il cross-compiler |

Comando unico per installare tutto su Ubuntu:

```bash
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo wget \
                 nasm mtools dosfstools python3 python3-parted scons \
                 qemu-system-x86 bochs bochs-sdl bochsbios vgabios gdb
```

## 2. Assembler: NASM

- L'**assembly** è la rappresentazione leggibile del codice macchina: ogni riga (`mov ax, 5`)
  corrisponde a un'istruzione del processore.
- **NASM** (*Netwide Assembler*) legge i file `.asm` e produce:
  - `-f bin`: **byte grezzi**, esattamente quello che finirà sul disco (lo usiamo per l'MBR);
  - `-f elf`: un **file oggetto** da passare al linker insieme al codice C (stage1, stage2, kernel).
- NASM usa la **sintassi Intel**: `mov destinazione, sorgente`. GCC e GDB di default usano la
  sintassi AT&T (al contrario). Per questo in GDB conviene `set disassembly-flavor intel`.

## 3. Perché un cross-compiler

Il `gcc` installato su Ubuntu produce programmi **per Linux a 64 bit**: assume che sotto ci sia il
kernel Linux, la libreria C (`glibc`), il formato e le convenzioni di Linux. Il nostro kernel invece
gira **da solo**, su una CPU a 32 bit, senza niente sotto.

Serve quindi un compilatore che:
- produca codice **i686** (x86 a 32 bit);
- non dia per scontato nessun sistema operativo (il "target" si chiama **`i686-elf`**:
  architettura i686, formato ELF, nessun OS);
- non provi a linkare la `glibc`.

Il cross-compiler è fatto di due pezzi, entrambi compilati da sorgente con
`scripts/setup_toolchain.sh`:

1. **binutils** (versione 2.37): `i686-elf-as` (assembler), `i686-elf-ld` (linker),
   `i686-elf-objcopy`, `i686-elf-strip`, `i686-elf-readelf`…
2. **GCC** (versione 11.2.0): `i686-elf-gcc` e `i686-elf-g++`, più **libgcc**, una piccola
   libreria di supporto (per esempio le divisioni a 64 bit su una CPU a 32 bit).

Il risultato sta in `~/Desktop/ALE/JOURNEY/.toolchains/i686-elf/` (circa 1 GB), **fuori** dal
repository, così non finisce su GitHub.

## 4. Il sistema di build: SCons

- Nelle prime puntate si usava **make**. Dalla Parte 11 si usa **SCons**: i file di build
  (`SConstruct`, `SConscript`) sono **script Python**, quindi si possono scrivere funzioni vere
  (creare partizioni, formattare, copiare file…).
- SCons capisce da solo le dipendenze (se modifichi `fat.c`, ricompila solo `fat.c` e rilinka lo
  stage2) usando le **firme MD5** dei file, salvate in `.sconsign.dblite`.
- Comandi principali:

```bash
scons                    # compila tutto e crea build/i686_debug/image.img
scons run                # avvia in QEMU
scons debug              # QEMU + GDB
scons bochs              # Bochs
scons -c                 # cancella i file compilati
scons imageType=floppy   # sovrascrive un'opzione di build_scripts/config.py
```

## 5. Gli emulatori: QEMU e Bochs

Un **emulatore** simula un PC intero (CPU, RAM, BIOS, dischi, scheda video). Ci permette di
"accendere" il nostro sistema in un secondo, senza riavviare il computer vero e senza rischiare di
rovinare il disco.

- **QEMU** è veloce e si usa per le prove di tutti i giorni. Opzioni che usiamo:
  - `-drive file=image.img,format=raw,if=floppy` oppure `…,media=disk`: il disco;
  - `-m 32`: 32 MB di RAM;
  - `-debugcon stdio`: tutto ciò che il sistema scrive sulla **porta 0xE9** compare nel terminale
    (è il nostro canale di log);
  - `-S -gdb stdio`: parte in pausa e aspetta GDB;
  - `-d int,cpu_reset -no-reboot`: stampa ogni interrupt ed eccezione e non riavvia in caso di
    *triple fault* (utilissimo quando "si riavvia da solo").
- **Bochs** è molto più lento ma ha un **debugger integrato** (`display_library: sdl2,
  options="gui_debug"`) che mostra registri, memoria e istruzioni anche nel codice a 16 bit, dove
  GDB fa fatica.

## 6. Gli strumenti per l'immagine disco

- **mkfs.fat** crea un file system FAT12/16/32 dentro un file (con `--offset` anche dentro una
  partizione).
- **mtools** (`mcopy`, `mmd`, `mdir`) legge e scrive file in un'immagine FAT **senza montarla**,
  quindi senza `sudo`. Con la sintassi `immagine.img@@1048576` lavora sulla partizione che inizia al
  byte 1 048 576 (settore 2048).
- **pyparted** è il binding Python di **libparted** (la libreria del programma `parted`): crea la
  tabella delle partizioni.

## 7. Configurazione del progetto (`build_scripts/config.py`)

```python
imageType = 'disk'          # 'floppy' (1,44 MB, FAT12) oppure 'disk' (con partizione)
imageFS = 'fat32'           # fat12 / fat16 / fat32 (solo per 'disk')
imageSize = '250m'          # dimensione del disco (k/m/g)
toolchain = '../.toolchains'
mountMethod = 'mtools'      # come copiare i file: mtools (consigliato) / guestfs / mount
```
