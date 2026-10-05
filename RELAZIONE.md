# DACOS 0.2: relazione completa

> DACOS è un sistema operativo didattico che si avvia da solo e offre una **shell interattiva** in
> stile Linux, con file di testo modificabili salvati sul disco e il comando `neofetch`.
> Qui trovi: come si usa, i comandi fatti e quelli da fare, il funzionamento di ogni file, le
> dipendenze, come si compila, i test e i bug corretti.
> La **teoria** è nella cartella [`theory/`](theory/README.md), un capitolo per argomento.
> Aggiornata il 3 ottobre 2026.

---

## Indice

0. [DACOS in breve: la shell e i comandi](#0-dacos-in-breve-la-shell-e-i-comandi)
1. [Stato del progetto](#1-stato-del-progetto)
2. [La teoria (cartella theory)](#2-la-teoria)
3. [Architettura: dall'accensione al kernel](#3-architettura-dallaccensione-al-kernel)
4. [Struttura del repository](#4-struttura-del-repository)
5. [File per file](#5-file-per-file)
6. [Dipendenze](#6-dipendenze)
7. [Come si compila e si avvia](#7-come-si-compila-e-si-avvia)
8. [Bug corretti](#8-bug-corretti)
9. [Limiti noti (non sono bug)](#9-limiti-noti-non-sono-bug)
10. [Glossario](#10-glossario)
11. [Riferimenti](#11-riferimenti)

---

## 0. DACOS in breve: la shell e i comandi

### 0.1 Cosa fa

All'avvio DACOS mostra il prompt della sua shell:

```
Benvenuti in DACOS 0.2!
Scrivi 'help' per la lista dei comandi, 'neofetch' per le informazioni sul sistema.

dacos@DACOS:/$ _
```

Da lì puoi muoverti tra le cartelle, creare, modificare, rinominare, spostare e cancellare file e
cartelle, scrivere testi con un editor a schermo intero, e vedere `neofetch` con la papera di
`theory/ascii_art.txt`. **I file vengono salvati davvero sul disco** (l'immagine
`build/i686_debug/image.img`): restano dopo il riavvio e si possono leggere anche da Linux.

### 0.2 Come si avvia e si usa

```sh
cd CARTELLA_PROGETTO
scons run
```

Si apre la finestra di QEMU: clicca dentro e scrivi. La tastiera è **italiana** (à è ò ù ì, AltGr
per `@ # [ ]`); con `keymap us` passi a quella americana. **Ctrl+Alt+G** libera il mouse dalla
finestra. Nel terminale da cui hai lanciato `scons run` compare una copia di tutta la sessione.

Una prova da fare subito:

```
dacos@DACOS:/$ mkdir appunti
dacos@DACOS:/$ cd appunti
dacos@DACOS:/appunti$ edit "lista della spesa.txt"      (scrivi, poi Ctrl+S salva e Ctrl+Q esci)
dacos@DACOS:/appunti$ cat "lista della spesa.txt"
dacos@DACOS:/appunti$ mv "lista della spesa.txt" spesa.txt
dacos@DACOS:/appunti$ echo "comprare il latte" >> spesa.txt
dacos@DACOS:/appunti$ ls -l
dacos@DACOS:/appunti$ cd ..
dacos@DACOS:/$ tree
dacos@DACOS:/$ neofetch
```

**Tasti della shell:** <-- --> per correggere la riga, ↑ ↓ per i comandi precedenti, **Tab** per
completare comandi e nomi di file, Ctrl+C per annullare la riga, Ctrl+L per pulire lo schermo.

**Tasti dell'editor:** frecce, Home/Fine, PagSu/PagGiù, Backspace/Canc, Tab (4 spazi),
**Ctrl+S** salva, **Ctrl+Q** esce (chiede conferma se non hai salvato), Ctrl+K cancella la riga.

> ⚠️ **Ricompilare cancella i tuoi file.** Ogni volta che modifichi il codice e rilanci `scons`,
> l'immagine del disco viene ricreata da zero, con solo i file di `image/root/`. Per conservare quello
> che hai creato dentro DACOS, prima di ricompilare esegui **`scons savefiles`**: copia i tuoi file
> dall'immagine in `image/root/`, e da lì vengono rimessi nell'immagine nuova.

### 0.3 Comandi implementati

Tutti quelli della lista in `idee/comandi_DACOS.txt` (in grassetto), più altri utili.

| Comando | Cosa fa | Esempio | Come è fatto |
|---|---|---|---|
| **`ls`** `[-l] [-a] [percorso]` | elenca file e cartelle, ordinati, in colonne; cartelle in blu; `-l` con dimensione e data, `-a` anche i nomi che iniziano con `.` | `ls -l /docs` | legge le voci della cartella dal FAT (nomi lunghi compresi) |
| **`cd`** `[cartella]` | cambia cartella; `cd ..`, `cd /`, `cd` da solo torna a `/` | `cd ../progetti` | aggiorna la stringa della cartella corrente, dopo aver controllato che esista |
| `pwd` | mostra la cartella corrente | `pwd` | |
| **`mkdir`** `[-p] cartella...` | crea cartelle; `-p` crea anche quelle intermedie | `mkdir -p a/b/c` | alloca un cluster libero, ci scrive `.` e `..`, aggiunge la voce con attributo "cartella" |
| `rmdir` `cartella...` | cancella una cartella **vuota** | `rmdir vecchia` | |
| **`touch`** / **`create`** `file...` | crea un file vuoto (se c'è già, aggiorna la data) | `touch nota.txt` | voce di dimensione 0, nessun cluster |
| **`cat`** / **`type`** `file...` | mostra il contenuto di uno o più file | `cat nota.txt` | segue la catena dei cluster e stampa i byte |
| `edit` / `nano` `file` | **editor di testo** a schermo intero: crea o modifica e salva il file (fino a 64 KB) | `edit diario.txt` | cap. 16 della teoria |
| **`rm`** / **`delete`** `[-r] percorso...` | cancella file; con `-r` anche cartelle con tutto dentro | `rm -r vecchi` | segna le voci come cancellate (`0xE5`) e libera i cluster |
| `mv` / `rename` `origine destinazione` | **rinomina** o **sposta** file e cartelle (in una cartella esistente: ci finisce dentro) | `mv a.txt b.txt`, `mv a.txt docs/` | nuova voce che punta agli stessi cluster + cancellazione della vecchia |
| `cp` `origine destinazione` | copia un file (fino a 64 KB) | `cp nota.txt copia.txt` | |
| **`tree`** `[cartella]` | albero grafico di cartelle e file, con il totale | `tree /` | funzione ricorsiva, rami disegnati con i caratteri `├── └── │` |
| `echo` `[-n] testo` | scrive il testo; con `>` o `>>` lo salva in un file | `echo ciao > a.txt` | |
| `>` e `>>` (con qualsiasi comando) | **redirezione**: l'output va in un file (sovrascrive / aggiunge in fondo) | `ls -l > elenco.txt`, `help >> a.txt` | cattura di stdout nel VFS |
| `neofetch` | la **papera di DACOS** a colori + informazioni sul sistema | `neofetch` | disegno da `theory/ascii_art.txt` |
| **`clear`** | pulisce lo schermo (anche Ctrl+L) | `clear` | |
| **`help`** `[comando]` | elenco dei comandi per categoria; `help comando` spiega come si usa | `help mv` | |
| `date` | data e ora (dall'orologio del PC) | `date` | RTC del chip CMOS |
| `uptime` | da quanto è acceso il sistema | `uptime` | timer PIT a 100 Hz |
| `free` | RAM utilizzabile, dimensione del kernel, mappa della memoria | `free` | mappa E820 passata dal bootloader |
| `df` | spazio totale, usato e libero sul disco | `df` | |
| `uname` `[-a]` | nome e versione del sistema | `uname -a` | |
| `whoami` | nome dell'utente (`dacos`) | `whoami` | |
| `history` | i comandi scritti finora | `history` | |
| `keymap` `[it\|us]` | cambia il layout della tastiera | `keymap us` | |
| **`reboot`** | riavvia | `reboot` | reset tramite il controller della tastiera |
| **`shutdown`** | spegne (in QEMU, Bochs e VirtualBox si spegne davvero) | `shutdown` | porte di spegnimento ACPI degli emulatori |

**Nomi dei file:** fino a 64 caratteri, con spazi, maiuscole e minuscole, lettere accentate (per i
nomi con spazi usa le virgolette: `cat "Nome con spazi.txt"`). Non sono ammessi `\ / : * ? " < > |`.
La cartella **`/boot`** (contiene il kernel) è protetta: si può leggere ma non modificare.


### 0.4 Dove sta il codice nuovo

| Parte | File | Teoria |
|---|---|---|
| Timer, orologio, tastiera | `src/kernel/arch/i686/pit.c`, `rtc.c`, `keyboard.c` | [cap. 14](theory/14_Timer_orologio_e_tastiera.md) |
| Disco ATA | `src/kernel/arch/i686/ata.c` | [cap. 15](theory/15_Disco_ATA_e_FAT_in_scrittura.md) |
| File system FAT in lettura e scrittura | `src/kernel/fs/fat.c`, `path.c` | [cap. 15](theory/15_Disco_ATA_e_FAT_in_scrittura.md) |
| Shell, comandi, editor, neofetch | `src/kernel/shell/` | [cap. 16](theory/16_La_shell_editor_e_neofetch.md) |
| Riavvio, spegnimento, CPU | `src/kernel/arch/i686/power.c`, `cpu.c` | [cap. 16](theory/16_La_shell_editor_e_neofetch.md) |
| Schermo (colori, cursore), `printf` con larghezza, redirezione | `vga_text.c`, `stdio.c`, `hal/vfs.c` | [cap. 7](theory/07_Scrivere_a_schermo_VGA_E9_e_printf.md), [cap. 16](theory/16_La_shell_editor_e_neofetch.md) |

### 0.5 Come l'ho verificato

Ho fatto girare la shell in QEMU **premendo i tasti in automatico** (comando `sendkey` del monitor di
QEMU), con screenshot e log, e alla fine ho controllato l'immagine del disco **da Linux** con `mtools`
e con `fsck.fat` (che verifica la coerenza del file system).

| Prova | Risultato |
|---|---|
| tutti i comandi della tabella 0.3, con argomenti giusti e sbagliati | ✅ funzionano; gli errori danno messaggi chiari (`cat: x.txt: file o cartella inesistente`) |
| file e cartelle creati in DACOS letti da Linux (`mdir`, `mtype`) | ✅ identici |
| `fsck.fat` dopo ogni sessione (FAT32 e FAT16) | ✅ **nessun errore** |
| riavvio: i file restano | ✅ |
| nomi lunghi con spazi, maiuscole, accenti; rinomina; spostamento di cartelle | ✅ |
| 40 file a nome lungo in una cartella (la cartella cresce su più cluster) | ✅ (`fsck` pulito) |
| editor: scrittura, frecce, Backspace, salvataggio, uscita senza salvare | ✅ |
| tastiera italiana: `àèòùì @ # []` | ✅ |
| Tab, cronologia, redirezione `>` e `>>` | ✅ |
| protezione di `/boot` | ✅ `rm /boot/kernel.elf` viene rifiutato |
| `reboot` e `shutdown` | ✅ QEMU si riavvia / si spegne |
| avvio della shell in Bochs | ✅ |
| versione floppy (senza disco) | ✅ la shell parte e avvisa che i comandi sui file non sono disponibili |
| `scons savefiles` + ricompilazione | ✅ i file tornano nell'immagine nuova |

---

## 1. Stato del progetto

### 1.1 Da dove viene il codice

La base è il branch **`master`** di
[nanobyte-dev/nanobyte_os](https://github.com/nanobyte-dev/nanobyte_os), cioè il
codice della della serie "Building an OS", con le correzioni agli script che l'autore ha fatto dopo il video.


Sopra quella base ho anche aggiunto tre cose che mancavano:

- un **MBR** proprio (`src/bootloader/mbr/`), con lettura LBA;
- uno **stack per il kernel** (`src/kernel/entry.asm`);
- la **lettura LBA** nello stage2.

Poi, sopra questa base corretta, ho costruito **DACOS 0.2**: driver per tastiera, timer, orologio e
disco, un file system FAT in scrittura e la shell con editor e neofetch.


### 1.2 Test della base: bootloader e kernel (tutti superati)

Questi test riguardano l'avvio (prima della shell).

Compilato con il cross-compiler `i686-elf` (binutils 2.37, gcc 11.2.0) installato in
`~/Desktop/ALE/JOURNEY/.toolchains/`, **0 errori e 0 warning**, senza sudo.

| # | Scenario | Emulatore | Risultato |
|---|---|---|---|
| 1 | disk FAT32 250 MB (default) | QEMU | ✅ kernel avviato, 6 regioni di memoria, 0 eccezioni |
| 2 | floppy FAT12 | QEMU | ✅ |
| 3 | disk FAT16 | QEMU | ✅ |
| 4 | build `release` (-O3) | QEMU | ✅ |
| 5 | disk da 1 GB | QEMU | ✅ |
| 6 | kernel da 216 KB (> 64 KB) su disk | QEMU | ✅ (prima: crash) |
| 7 | kernel da 216 KB su floppy | QEMU | ✅ |
| 8 | stage1 **e** stage2 costretti a usare il CHS (BIOS senza estensioni), floppy e disk, kernel da 216 KB | QEMU | ✅ |
| 9 | MBR costretto a usare il CHS | QEMU | ✅ |
| 10 | root FAT32 di 76 cluster tutti non contigui, con 300 file a nome lungo | QEMU | ✅ kernel trovato |
| 11 | kernel mancante | QEMU | ✅ messaggio `ELF: can't open /boot/nokernel.elf`, nessun crash |
| 12 | `crash_me()` (eccezione) | QEMU | ✅ KERNEL PANIC corretto: eccezione 11, codice di errore 0x402 = IDT voce 0x80 |
| 13 | timer (IRQ 0) abilitato | QEMU | ✅ ~18 tick al secondo |
| 14 | disk FAT32 | **Bochs** | ✅ (prima: non partiva) |
| 15 | floppy | **Bochs** | ✅ |
| 16 | debug con GDB: breakpoint su `start`, backtrace | QEMU + GDB | ✅ |
| 17 | `tools/fat` (lettura file e cartelle, file inesistente) | Linux | ✅ (prima: ciclo infinito) |

Output reale della base, prima della shell (porta E9, disk FAT32):

```
E820: base=0x0 length=0x9fc00 type=0x1
E820: base=0x9fc00 length=0x400 type=0x2
E820: base=0xf0000 length=0x10000 type=0x2
E820: base=0x100000 length=0x1ee0000 type=0x1
E820: base=0x1fe0000 length=0x20000 type=0x2
E820: base=0xfffc0000 length=0x40000 type=0x2
[PIC] Found 8259 PIC.
[Main] Boot device: 80
[Main] Memory region count: 6
[Main] MEM: start=0x0 length=0x9fc00 type=1
...
[Main] This is an info msg!
[Main] This is a warning msg!
[Main] This is an error msg!
[Main] This is a critical msg!
```

A schermo allora c'era solo `Nanobyte OS v0.1`; ora c'è la shell di DACOS.

**Non testati** (non potevo farlo da qui): i metodi di mount `mount` (richiede la tua password sudo)
e `guestfs` (su Ubuntu non funziona da utente normale, 7.5), e l'avvio su un **PC vero**.

---

## 2. La teoria

È nella cartella [`theory/`](theory/README.md), scritta come appunti, un capitolo per argomento,
con esempi presi dall'immagine compilata:

| # | Capitolo |
|---|---|
| 0 | [Gli strumenti](theory/00_Strumenti.md) |
| 1 | [Come si avvia un computer](theory/01_Come_si_avvia_un_computer.md): BIOS, 0x7C00, real mode, registri, segmentazione, stack, interrupt del BIOS |
| 2 | [Leggere dal disco](theory/02_Leggere_dal_disco.md): CHS, LBA, `int 0x13`, estensioni |
| 3 | [Il file system FAT](theory/03_Il_file_system_FAT.md): esempio svolto sul nostro floppy |
| 4 | [Partizioni, MBR e bootloader a due stadi](theory/04_Partizioni_MBR_e_bootloader_a_due_stadi.md) |
| 5 | [Protected mode, A20 e GDT](theory/05_Protected_mode_A20_e_GDT.md) |
| 6 | [Il C senza sistema operativo](theory/06_C_senza_sistema_operativo.md): freestanding, cdecl, linker script |
| 7 | [Scrivere a schermo](theory/07_Scrivere_a_schermo_VGA_E9_e_printf.md): VGA, porte di I/O, E9, `printf` |
| 8 | [Il formato ELF e il caricamento del kernel](theory/08_Formato_ELF_e_caricamento_del_kernel.md) |
| 9 | [Interrupt ed eccezioni](theory/09_Interrupt_ed_eccezioni_IDT_e_ISR.md): IDT, ISR, kernel panic |
| 10 | [PIC 8259 e IRQ](theory/10_PIC_8259_e_IRQ.md), con l'esercizio del timer |
| 11 | [Rilevare la memoria (E820)](theory/11_Rilevare_la_memoria_E820.md) |
| 12 | [Il sistema di build (SCons)](theory/12_Il_sistema_di_build_SCons.md) |
| 13 | [Debug](theory/13_Debug.md): QEMU, GDB, Bochs, metodo |
| 14 | [Timer, orologio e tastiera](theory/14_Timer_orologio_e_tastiera.md): PIT, RTC, scancode, layout italiano |
| 15 | [Disco ATA e FAT in scrittura](theory/15_Disco_ATA_e_FAT_in_scrittura.md): driver del disco, creare/cancellare file, nomi lunghi |
| 16 | [La shell, l'editor e neofetch](theory/16_La_shell_editor_e_neofetch.md): line editor, parsing, redirezione, comandi |

---

## 3. Architettura: dall'accensione al kernel

### 3.1 Il flusso di avvio (immagine "disk")

```
 Accensione --> BIOS (POST) --> legge il settore 0 del disco in 0x7C00, DL = 0x80
     │
     ▼
 MBR  (src/bootloader/mbr/mbr.asm, 16 bit, 440 byte)
     │  si sposta a 0x0600, trova la partizione attiva,
     │  legge il suo primo settore (LBA 2048) in 0x7C00: LBA (int 13h/42h) o CHS (02h)
     │  salta con DL = disco, DS:SI --> voce della partizione
     ▼
 STAGE1  (src/bootloader/stage1/boot.asm, VBR, 512 byte)
     │  copia la voce di partizione in 0x10000, DS=ES=SS=0, SP=0x7C00
     │  int 13h/41h: estensioni LBA?
     │  legge lo stage2 (LBA 1, 25 settori, uno alla volta) in 0x0500
     │  salta a 0000:0500 con DL = disco, DI:SI = voce di partizione
     ▼
 STAGE2  (src/bootloader/stage2/, 16 --> 32 bit)
     │  entry.asm: stack 0xFFF0, A20, GDT, CR0.PE, far jump --> protected mode,
     │             tutti i segmenti = 0x10, azzera la .bss, _init()
     │  main.c start():
     │     DISK_Initialize      int 13h/08h (geometria) + 41h (LBA?)   [torna in real mode e rientra]
     │     MBR_DetectPartition  offset della partizione
     │     FAT_Initialize       boot sector, tipo FAT, root directory
     │     Memory_Detect        int 15h/E820 --> 6 regioni
     │     ELF_Read             /boot/kernel.elf --> segmenti a 0x100000
     │     kernelEntry(&g_BootParams)
     ▼
 KERNEL  (src/kernel/, 32 bit, a 0x100000)
        entry.asm: stack proprio da 64 KB --> start(bootParams)
        _init(); HAL_Initialize():
            VGA_clrscr · GDT · IDT · ISR (256 gestori) · IRQ (PIC rimappato su 0x20, sti)
            PIT a 100 Hz (IRQ 0) · tastiera PS/2 (IRQ 1)
        ATA_Initialize (disco) --> FAT_Mount (trova la partizione FAT nell'MBR)
        Shell_Run(): "dacos@DACOS:/$ " --> legge i tasti --> esegue i comandi --> ...
```

Con il **floppy** non c'è MBR: il BIOS carica direttamente lo stage1 dal settore 0, e lo stage2 sta
nei settori riservati del FAT12.

### 3.2 Mappa della memoria

| Indirizzo | Contenuto |
|---|---|
| `0x00000–0x004FF` | IVT e BDA del BIOS |
| `0x00500–0x04EEB` | **stage2** (codice + dati + `.bss`, ~19 KB; il linker verifica che resti sotto `0xE000`) |
| `0x00600–0x007FF` | MBR spostato (solo durante il boot, poi sovrascritto dallo stage2) |
| `0x07C00–0x07DFF` | stage1 |
| `…–0x0FFF0` | stack dello stage2 (cresce verso il basso) |
| `0x10000–0x1000F` | copia della voce di partizione |
| `0x20000–0x2FFFF` | dati del driver FAT |
| `0x30000–0x3FFFF` | header ELF |
| `0x40000–0x4FFFF` | buffer di caricamento del kernel |
| `0x9FC00–0xFFFFF` | EBDA, video (`0xB8000` = testo), ROM |
| `0x100000–0x155950` | **kernel**: 66 KB di codice e dati + 277 KB di `.bss` (stack da 64 KB, buffer di 64 KB per i file e per la redirezione, cache dei settori) |

### 3.3 Layout delle immagini

```
DISK (250 MB, FAT32)                       FLOPPY (1,44 MB, FAT12)
settore 0       MBR + tabella partizioni   settore 0      BPB (mkfs.fat) + stage1
settori 1–25    stage2                     settori 1–25   stage2 (settori riservati)
settore 2048    BPB (mkfs.fat) + stage1    settori 26–43  2 FAT
settore 2049    FSInfo                     settori 44–57  root directory
settore 2054    backup del boot sector     settore 58–    dati
settori 2080–   2 FAT da 3923 settori
settore 9926–   dati (root = cluster 2)
file: /boot/kernel.elf, /test.txt, /folder/demo.txt
```

---

## 4. Struttura del repository

```
DACOS/
├── RELAZIONE.md                questo file
├── theory/                     la teoria, 17 capitoli (+ ascii_art.txt, la papera)
├── SConstruct                  build principale (SCons)
├── requirements.txt            pyparted
├── README.md, LICENSE          di nanobyte_os
├── .vscode/                    configurazione di VS Code e script GDB
├── build_scripts/
│   ├── config.py               configurazione della build
│   ├── utility.py              funzioni di supporto
│   ├── phony_targets.py        target run/debug/bochs/toolchain/savefiles
│   ├── generate_isrs.sh        genera isrs_gen.c / .inc
│   └── generate_ascii_art.py   theory/ascii_art.txt --> shell/ascii_art.h       <-- NUOVO
├── scripts/
│   ├── setup_toolchain.sh      compila binutils + gcc i686-elf
│   ├── install_deps.sh         installa i pacchetti (incompleto, meglio 6)
│   ├── run.sh, debug.sh, bochs.sh
│   └── save_files.sh           salva i file creati in DACOS in image/root/     <-- NUOVO
├── image/
│   ├── SConscript              costruisce l'immagine
│   └── root/                   file copiati nella radice (test.txt, folder/demo.txt)
├── src/
│   ├── bootloader/
│   │   ├── mbr/                mbr.asm, SConscript               <-- NUOVO
│   │   ├── stage1/             boot.asm, linker.ld, SConscript
│   │   └── stage2/             entry.asm, main.c, x86.asm/.h, disk, mbr, fat, elf,
│   │                           memdetect, memory, stdio, string, stdlib, ctype,
│   │                           minmax.h, memdefs.h, crti/crtn.asm, linker.ld
│   ├── kernel/
│   │   ├── entry.asm                                             <-- NUOVO
│   │   ├── main.c, debug, stdio, memory, string, crti/crtn.asm, linker.ld
│   │   ├── hal/                hal, vfs
│   │   ├── util/               arrays.h, binary.h
│   │   ├── fs/                 fat.c/.h (FAT in scrittura), path.c/.h      <-- NUOVO
│   │   ├── shell/              shell.c, commands.c, editor.c, neofetch.c,
│   │   │                       ascii_art.h (generato), shell.h            <-- NUOVO
│   │   └── arch/i686/          gdt, idt, isr, irq, i8259, pic.h, io, e9, vga_text,
│   │                           isrs_gen.c/.inc (generati),
│   │                           pit, rtc, keyboard, ata, power, cpu       <-- NUOVI
│   └── libs/
│       ├── boot/bootparams.h   struttura condivisa stage2 <--> kernel
│       └── core/               libreria C++ (inizio del refactoring)
├── tools/fat/                  programma per Linux che legge un'immagine FAT12
└── idee/                       appunti
```

---

## 5. File per file

### 5.1 Build

| File | Cosa fa |
|---|---|
| `SConstruct` | dichiara le opzioni (`config`, `arch`, `imageType`, `imageFS`, `imageSize`, `toolchain`, `mountMethod`), crea l'ambiente host e quello target (cross-compiler + `-ffreestanding -nostdlib`, `-lgcc`), richiama i `SConscript` (libcore --> mbr --> stage1 --> stage2 --> kernel --> image) e definisce `run`, `debug`, `bochs`, `toolchain`. `DEPS['gcc']` deve corrispondere alla versione di gcc compilata, perché serve a trovare `crtbegin.o`/`libgcc.a` |
| `build_scripts/config.py` | valori di default: `disk`, `fat32`, `250m`, `../.toolchains`, `mtools` |
| `build_scripts/utility.py` | `ParseSize` ("250m" --> byte), `GlobRecursive` (glob ricorsivo), `FindIndex`, `IsFileName`, `RemoveSuffix` |
| `build_scripts/phony_targets.py` | alias SCons che eseguono sempre un comando |
| `build_scripts/generate_isrs.sh` | genera i 256 gestori di interrupt (va rilanciato a mano se cambi la lista delle eccezioni con codice di errore) |
| `*/SConscript` | per ogni modulo: compila `*.c *.cpp *.asm` (glob ricorsivo), linka con il proprio `linker.ld`, produce un file `.map`, mette `crti/crtbegin … crtend/crtn` nell'ordine giusto |
| `src/bootloader/mbr/SConscript` | `nasm -f bin` --> `mbr.bin` (440 byte) |
| `image/SConscript` | costruisce l'immagine: file vuoto (`truncate`), tabella delle partizioni (pyparted, partizione attiva da 2048, tipo FAT), **MBR** (primi 440 byte), `mkfs.fat`, stage1 (preserva il BPB e scrive la posizione dello stage2), stage2 (settori 1–N, con controllo che stia prima della partizione), copia dei file con **mtools** (oppure guestmount o sudo mount). FAT32 con 32 settori riservati e lo stage1 copiato anche nel backup del boot sector (così `fsck.fat` non trova differenze); etichetta del volume `DACOS`. Usa `subprocess`: non serve più il modulo Python `sh` |
| `build_scripts/generate_ascii_art.py` | converte `theory/ascii_art.txt` (UTF-8) in `src/kernel/shell/ascii_art.h` (code page 437): toglie i margini e, se serve, una di due righe uguali per stare in 24 righe |

### 5.2 Script

| Script | Cosa fa |
|---|---|
| `setup_toolchain.sh <dir>` | scarica e compila binutils 2.37 e gcc 11.2.0 per `i686-elf` in `<dir>/i686-elf` (le versioni si cambiano con `BINUTILS_VERSION` e `GCC_VERSION`; `-c` pulisce) |
| `run.sh <tipo> <img>` | `qemu-system-i386 -debugcon stdio -m 32 -drive file=…,format=raw` |
| `debug.sh <tipo> <img>` | QEMU in pausa + GDB con i simboli di `kernel.elf` |
| `bochs.sh <tipo> <img>` | genera `.bochs_config` (BIOS legacy, `sdl2` + debugger grafico, geometria del disco calcolata da Bochs) e avvia Bochs |
| `install_deps.sh` | installa i pacchetti in base alla distribuzione (non tutti quelli necessari: usa la lista del 6) |
| `save_files.sh <tipo> <img>` (`scons savefiles`) | copia tutti i file dell'immagine (tranne `/boot`) in `image/root/`, così sopravvivono alla prossima ricompilazione |

### 5.3 `src/bootloader/mbr/mbr.asm` (nuovo)
MBR di 440 byte: si sposta a `0x0600`, cerca la voce con il bit `0x80`, legge il primo settore della
partizione in `0x7C00` con `int 13h/42h` (LBA dalla tabella) o, se le estensioni mancano, con
`int 13h/02h` (CHS dalla tabella), controlla `0xAA55` e salta con `DL` = disco e `DS:SI` = voce.
Stampa `MBR: no active partition` / `read error` / `partition not bootable` se qualcosa va storto.

### 5.4 `src/bootloader/stage1/`
- **`boot.asm`**: salto + BPB/EBR segnaposto (`-DFILESYSTEM=fat12|fat16|fat32` sceglie il formato),
  poi `start`: copia la voce di partizione a `0x1000:0000`, imposta segmenti e stack, normalizza `CS`,
  controlla le estensioni LBA, legge lo stage2 **un settore alla volta** (LBA o CHS) seguendo la tabella
  `stage2_location`, salta a `0000:0500`. Funzioni: `puts`, `lba_to_chs`, `disk_read` (3 tentativi),
  `disk_reset`, `wait_key_and_reboot`.
- **`linker.ld`**: origine `0x7C00`, formato `binary`, firma `0xAA55` a `0x7DFE` (il linker si
  rifiuta di compilare se il codice la supera).

### 5.5 `src/bootloader/stage2/`

| File | Cosa fa |
|---|---|
| `entry.asm` | punto di ingresso: salva disco e partizione, stack, A20 tramite l'8042, GDT (null, code32, data32, code16, data16), protected mode, **carica DS ES FS GS SS**, azzera la `.bss`, `_init`, `start(bootDrive, partition)` |
| `main.c` | `start`: disco --> partizione --> FAT --> mappa di memoria --> ELF --> salto al kernel |
| `x86.asm` / `x86.h` | ponte verso il BIOS: macro per entrare e uscire dal protected mode, `LinearToSegOffset`, `x86_outb/inb`, `x86_Disk_GetDriveParams`, `x86_Disk_Reset`, `x86_Disk_Read` (CHS), **`x86_Disk_ExtensionsPresent`, `x86_Disk_ExtendedRead`** (LBA, nuove), `x86_E820GetNextBlock` |
| `disk.c` / `disk.h` | `DISK_Initialize` (geometria + estensioni), `DISK_ReadSectors`: LBA se c'è, altrimenti CHS un settore alla volta; 3 tentativi con reset |
| `mbr.c` / `mbr.h` | `Partition`: floppy = tutto il disco; disco = offset e dimensione dalla voce dell'MBR. `Partition_ReadSectors` aggiunge l'offset |
| `fat.c` / `fat.h` | driver FAT12/16/32 in sola lettura: boot sector, tipo FAT, root (fissa in FAT12/16, catena di cluster in FAT32), `FAT_Open` (percorsi con `/`), `FAT_Read`, `FAT_NextCluster` (cache di 5 settori), nomi 8.3 |
| `elf.c` / `elf.h` | `ELF_Read`: valida l'header, legge il program header, carica i segmenti `PT_LOAD` (sopra 1 MB) passando dal buffer `0x40000` |
| `memdetect.c` / `.h` | `Memory_Detect`: E820 --> `g_MemRegions` (max 256) --> `MemoryInfo` |
| `memdefs.h` | mappa della memoria dello stage2 (3.2) |
| `stdio.c` / `.h` | VGA testo (+ copia su E9), `printf` (`%c %s %d %i %u %x %X %p %o %%`, `h hh l ll`), `print_buffer` |
| `memory.c` / `.h` | `memcpy`, `memset`, `memcmp` (con `size_t`), `segoffset_to_linear` |
| `string.c` / `.h` | `strchr`, `strcpy`, `strlen`, `strcmp`, conversioni UTF-16 --> UTF-8 (per i nomi lunghi) |
| `stdlib.c` / `.h` | `qsort` (insertion sort) |
| `ctype.c` / `.h`, `minmax.h` | `islower`, `toupper`, `min`, `max` |
| `crti.asm`, `crtn.asm` | prologo ed epilogo di `_init`/`_fini` |
| `linker.ld` | origine `0x500`, `binary`, `.entry` in testa, `ASSERT(__end <= 0xE000)` |

### 5.6 `src/libs/boot/bootparams.h`
`BootParams { MemoryInfo Memory; uint8_t BootDevice; }`, il contratto tra stage2 e kernel.

### 5.7 `src/kernel/`

| File | Cosa fa |
|---|---|
| `entry.asm` (nuovo) | entry point dell'ELF: prende `BootParams*`, passa a uno **stack da 64 KB** nella `.bss` del kernel, chiama `start` |
| `main.c` | `start`: `_init`, `HAL_Initialize`, log della mappa di memoria, disco ATA e montaggio del FAT, poi `Shell_Run` |
| `string.c` / `.h` (nuovo) | `strlen`, `strcmp`, `strcasecmp`, `strlcpy`, `strlcat`, `strchr`, `strrchr`, `memmove`, `toupper`, `atoi`… |
| `fs/fat.c` / `.h` (nuovo) | **driver FAT12/16/32 in lettura e scrittura**: monta la partizione, cache dei settori write-through, lettura/scrittura della FAT, allocazione dei cluster, cartelle, nomi lunghi (lettura e scrittura), `FAT_Lookup`, `FAT_ListDirectory`, `FAT_ReadStream`, `FAT_ReadFile`, `FAT_WriteFile`, `FAT_Touch`, `FAT_MakeDirectory`, `FAT_Remove`, `FAT_Rename`; `/boot` protetto |
| `fs/path.c` / `.h` (nuovo) | `Path_Resolve` (percorsi relativi, `.` e `..`), `Path_Split`, `Path_IsInside` |
| `shell/shell.c` (nuovo) | la shell **dsh**: prompt, line editor, cronologia, Tab, parsing con virgolette, redirezione `>` / `>>` |
| `shell/commands.c` (nuovo) | tutti i comandi (0.3) e la tabella `g_Commands` |
| `shell/editor.c` (nuovo) | l'editor di testo a schermo intero |
| `shell/neofetch.c`, `ascii_art.h` (nuovi) | `neofetch` con la papera |
| `arch/i686/pit.c` (nuovo) | timer a 100 Hz, uptime |
| `arch/i686/rtc.c` (nuovo) | data e ora dall'RTC (CMOS) |
| `arch/i686/keyboard.c` (nuovo) | tastiera PS/2: scancode set 1, layout `it` e `us`, Shift/Ctrl/AltGr/Caps Lock, buffer circolare |
| `arch/i686/ata.c` (nuovo) | disco ATA in PIO: IDENTIFY, lettura, scrittura, flush |
| `arch/i686/power.c`, `cpu.c` (nuovi) | `reboot`, `shutdown`; nome della CPU con `cpuid` |
| `linker.ld` | origine `0x100000`, `elf32-i386`, `ENTRY(entry)` |
| `debug.c` / `.h` | `logf` + `log_debug/info/warn/err/crit`, colori ANSI su E9 |
| `stdio.c` / `.h` | `fputc/fputs/vfprintf/fprintf` su file descriptor; `printf` = stdout, `debugf` = E9; supporta larghezza e allineamento (`%-10s`, `%08x`) |
| `memory.c` / `.h` | `memcpy/memset/memcmp` (`size_t`) |
| `hal/hal.c` | ordine di inizializzazione: VGA --> GDT --> IDT --> ISR --> IRQ --> PIT --> tastiera |
| `hal/vfs.c` / `.h` | fd 1/2 --> VGA (e copia su E9 convertita in UTF-8), fd 3 --> E9; cattura di stdout per la redirezione |
| `util/arrays.h`, `binary.h` | `SIZE()`, `FLAG_SET/UNSET` |
| `arch/i686/gdt.c`, `gdt_asm.asm` | GDT del kernel e `i686_GDT_Load` (lgdt + `retf` per ricaricare CS) |
| `arch/i686/idt.c/.h`, `idt_asm.asm` | `g_IDT[256]`, `SetGate`, `EnableGate/DisableGate`, `lidt` |
| `arch/i686/isr.c/.h`, `isr_asm.asm`, `isrs_gen.c/.inc` | 256 gestori, `isr_common`, `Registers`, `i686_ISR_Handler` (KERNEL PANIC per le eccezioni non gestite) |
| `arch/i686/pic.h`, `i8259.c/.h` | interfaccia `PICDriver` e driver 8259 (ICW1–4, maschera, EOI, probe) |
| `arch/i686/irq.c/.h` | rimappa il PIC su 0x20–0x2F, collega gli IRQ agli ISR, EOI, `sti` |
| `arch/i686/io.c/.h`, `io_asm.asm` | `outb/inb`, `sti/cli`, `iowait`, `Panic`, `crash_me` |
| `arch/i686/e9.c`, `vga_text.c` | porta E9 e driver VGA (colori, backspace, posizione del cursore, scrittura diretta di una cella) |
| `arch/i686/io_asm.asm` | anche `inw/outw/insw/outsw` (per l'ATA) e `i686_Halt` (`sti; hlt`) |

### 5.8 `src/libs/core/` (C++)
Inizio del passaggio al C++ delle puntate successive: `Defs.hpp`, `TypeTraits.hpp`,
`CharacterDevice`, `BlockDevice`, `TextDevice` (`printf` a oggetti), `E9Device`, `VGATextDevice`,
`IO.asm`, `File.hpp`. Viene compilata e linkata, ma **non è ancora usata**.

### 5.9 `tools/fat/`
Programma per Linux (dalla Parte 3) che legge un'immagine **FAT12 senza partizioni** (cioè il
floppy): `make -C tools/fat BUILD_DIR=build`, poi `build/tools/fat.out build/i686_debug/image.img
/test.txt` stampa il file, e con una cartella (`/boot`) ne elenca il contenuto.

---

## 6. Dipendenze

### 6.1 Pacchetti (Ubuntu)

```sh
sudo apt install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo wget \
                 nasm mtools dosfstools python3 python3-parted scons \
                 qemu-system-x86 bochs bochs-sdl bochsbios vgabios gdb
```

**Sul tuo PC c'è già tutto** (verificato). Il modulo Python `sh`, che prima mancava, non serve più.

### 6.2 Il cross-compiler
Già compilato e installato in `~/Desktop/ALE/JOURNEY/.toolchains/i686-elf/` (circa 1 GB):
`i686-elf-gcc (GCC) 11.2.0`, binutils 2.37. Per ricompilarlo da zero (12 minuti più il download):

```sh
scons toolchain        # = ./scripts/setup_toolchain.sh ../.toolchains
```

---

## 7. Come si compila e si avvia

### 7.1 In breve

```sh
cd ~/Desktop/ALE/JOURNEY/DACOS
scons            # compila tutto --> build/i686_debug/image.img (disk FAT32)
scons run        # avvia in QEMU
```

### 7.2 Opzioni

```sh
scons imageType=floppy        # floppy FAT12
scons imageFS=fat16           # disk con FAT16
scons imageSize=1g            # disk più grande
scons config=release          # ottimizzato (-O3), output in build/i686_release/
scons -c                      # pulisce
scons -h                      # elenco delle opzioni
```

Le opzioni da riga di comando valgono solo per quel comando: usa le stesse anche per `run`
(`scons run imageType=floppy`), oppure cambiale in `build_scripts/config.py`.

### 7.3 Debug

```sh
scons debug      # QEMU in pausa + GDB: (gdb) b start --> c --> n / s / bt / p variabile
scons bochs      # Bochs con il debugger grafico
```

Per vedere le eccezioni a mano:

```sh
qemu-system-i386 -m 32 -debugcon stdio -drive file=build/i686_debug/image.img,format=raw \
                 -d int,cpu_reset -D qemu.log -no-reboot
```

### 7.4 Cosa devi vedere
Nella finestra di QEMU il messaggio `Benvenuti in DACOS 0.2!` e il prompt `dacos@DACOS:/$`: da lì
si usa la shell (0). Nel terminale: le righe `E820: …`, i log del kernel (`[FAT] mounted FAT32…`) e
una copia di tutto quello che scrivi e che la shell stampa.

### 7.5 Salvare i file prima di ricompilare
```sh
scons savefiles     # copia i file creati in DACOS in image/root/
scons               # ricostruisce: i file di image/root/ tornano nell'immagine
```

### 7.6 Metodi per copiare i file nell'immagine (`mountMethod`)
- **`mtools`** (default): niente sudo, funziona ovunque.
- `guestfs`: su Ubuntu fallisce, perché `/boot/vmlinuz-*` è leggibile solo da root.
- `mount`: usa `sudo losetup` + `mount` e chiede la password.

> ⚠️ Non usare mai `sudo scons`: i file in `build/` diventerebbero di root.

### 7.7 Provare su un PC vero (facoltativo)
`sudo dd if=build/i686_debug/image.img of=/dev/sdX bs=4M status=progress` su una chiavetta USB
(**attenzione**: `sdX` deve essere la chiavetta, perché **cancella tutto** il dispositivo), poi avvio
in modalità **Legacy/CSM** (non UEFI). Non l'ho potuto testare.

---

## 8. Limiti noti (non sono bug)

Sono cose che il progetto, a questo punto della serie, semplicemente **non fa ancora**:

- il driver FAT del **bootloader** è in sola lettura e trova i file solo con il nome 8.3 (basta per
  `kernel.elf`); quello del **kernel** invece legge e scrive, con i nomi lunghi;
- al massimo 10 file aperti contemporaneamente nello stage2;
- `edit`, `cp` e `>>` lavorano su file fino a **64 KB** (buffer fisso, non c'è ancora `malloc`); `cat`
  legge file di qualsiasi dimensione; i nomi dei file fino a 64 caratteri;
- i comandi sui file funzionano solo con l'immagine **disk** (il kernel non ha un driver per il
  floppy) e con un disco ATA sul canale primario (quello di QEMU e Bochs);
- i file di testo usano la code page 437: le lettere accentate scritte in DACOS, aperte su Linux,
  vanno convertite (`iconv -f cp437 -t utf-8`); i nomi dei file invece sono in Unicode e si vedono
  giusti;
- `shutdown` spegne davvero solo negli emulatori; su un PC vero ferma la CPU e dice di spegnere a mano;
- lo stage2 deve stare sotto `0xE000` (~56 KB con la `.bss`) e in 255 settori; il linker e lo script
  avvisano se si supera;
- niente UEFI: serve un BIOS legacy (o la modalità CSM);
- il kernel per ora non usa la mappa della memoria (manca un allocatore) e la libreria C++ non è
  usata; non ci sono processi né utenti (vedi 0.4);
- nel KERNEL PANIC i valori `esp` e `ss` non sono significativi quando l'eccezione avviene già in
  ring 0 (la CPU li salva solo quando cambia il livello di privilegio).

---

## 9. Glossario

| Termine | Significato |
|---|---|
| **A20** | 21ª linea di indirizzo; va accesa per usare la memoria oltre 1 MB senza wrap-around |
| **BDA / EBDA** | aree dati del BIOS in memoria bassa |
| **BPB / EBR** | parametri del volume FAT nel boot sector |
| **cdecl** | convenzione di chiamata C a 32 bit (argomenti sullo stack, ritorno in EAX) |
| **CHS / LBA** | indirizzamento geometrico / lineare dei settori |
| **Chainloading** | un bootloader che carica il bootloader successivo (MBR --> VBR) |
| **Cluster** | unità di allocazione di FAT |
| **Cross-compiler** | compilatore che produce codice per una piattaforma diversa da quella su cui gira |
| **DAP** | Disk Address Packet, la struttura per `int 13h/42h` |
| **E820** | funzione del BIOS che restituisce la mappa della memoria |
| **ELF** | formato degli eseguibili Unix, usato per il kernel |
| **EOI** | End Of Interrupt, comando al PIC |
| **Freestanding** | C senza sistema operativo né libreria standard |
| **GDT / IDT / IVT** | tabella dei segmenti / degli interrupt in protected mode / degli interrupt in real mode |
| **IRQ / ISR** | richiesta di interrupt hardware / funzione che la gestisce |
| **MBR / VBR** | primo settore del disco / della partizione |
| **PIC 8259** | controller degli interrupt dei PC |
| **Real / protected mode** | modalità a 16 bit (8086) / a 32 bit con protezione |
| **Selettore** | valore di un registro di segmento in protected mode (indice nella GDT) |
| **Triple fault** | eccezione durante la gestione di un double fault: la CPU si resetta |

---

## 10. Riferimenti

- Serie "Building an OS" di nanobyte: <https://www.youtube.com/watch?v=9t-SPC7Tczc&list=PLFjM7v6KGMpiH2G-kT781ByCNC_0pKpPN>
  (Parte 11: <https://www.youtube.com/watch?v=xp-yB9WBadI>)
- Repository: <https://github.com/nanobyte-dev/nanobyte_os> e la pagina dei problemi noti:
  <https://github.com/nanobyte-dev/nanobyte_os/wiki/Frequent-issues>
- OSDev Wiki: <https://wiki.osdev.org/> (Boot Sequence, Real Mode, Protected Mode, A20 Line, GDT,
  IDT, Interrupts, 8259 PIC, Exceptions, FAT, MBR (x86), ELF, Detecting Memory (x86),
  GCC Cross-Compiler, Calling Global Constructors)
- Ralf Brown's Interrupt List (tutti gli interrupt del BIOS): <https://www.ctyme.com/rbrown.htm>
- Specifica Microsoft "FAT: General Overview of On-Disk Format"
- Intel® 64 and IA-32 Architectures Software Developer's Manual, Vol. 3A (cap. 2, 3, 6, 9)
