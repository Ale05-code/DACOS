# Teoria: indice

Appunti per capire **come funziona** il progetto, dall'accensione del PC al kernel. Ogni capitolo
spiega i concetti e poi mostra dove si trovano nel codice, con esempi presi dall'immagine vera
compilata (numeri letti da `build/i686_debug/image.img`, output reali di QEMU).

Conviene leggerli in ordine: ognuno usa i precedenti.

| # | Capitolo | Di cosa parla | File del progetto |
|---|---|---|---|
| 0 | [Gli strumenti](00_Strumenti.md) | NASM, cross-compiler, SCons, QEMU, Bochs, mtools | `scripts/`, `build_scripts/` |
| 1 | [Come si avvia un computer](01_Come_si_avvia_un_computer.md) | BIOS, 0x7C00, firma 0xAA55, real mode, registri, segmentazione, stack, `int 0x10` | `mbr/mbr.asm`, `stage1/boot.asm` |
| 2 | [Leggere dal disco](02_Leggere_dal_disco.md) | CHS e LBA, `int 0x13`, estensioni LBA, perché un settore alla volta | `stage1/boot.asm`, `stage2/disk.c`, `x86.asm` |
| 3 | [Il file system FAT](03_Il_file_system_FAT.md) | regioni, BPB, voci di directory, catena di cluster, FAT12/16/32, LFN | `stage2/fat.c`, `tools/fat/` |
| 4 | [Partizioni, MBR e bootloader a due stadi](04_Partizioni_MBR_e_bootloader_a_due_stadi.md) | MBR, tabella delle partizioni, chainloading, stage1 → stage2, layout dell'immagine | `mbr/`, `stage1/`, `image/SConscript` |
| 5 | [Protected mode, A20 e GDT](05_Protected_mode_A20_e_GDT.md) | 32 bit, linea A20, descrittori, entrare e uscire dal protected mode | `stage2/entry.asm`, `x86.asm`, `kernel/arch/i686/gdt.c` |
| 6 | [Il C senza sistema operativo](06_C_senza_sistema_operativo.md) | freestanding, cross-compiler, cdecl, linker script, `.bss`, crti/crtn, stack del kernel | `linker.ld`, `memory.c`, `kernel/entry.asm` |
| 7 | [Scrivere a schermo](07_Scrivere_a_schermo_VGA_E9_e_printf.md) | memoria video 0xB8000, cursore, porte di I/O, porta E9, colori ANSI, `printf` | `stdio.c`, `vga_text.c`, `debug.c`, `hal/vfs.c` |
| 8 | [Il formato ELF](08_Formato_ELF_e_caricamento_del_kernel.md) | header, segmenti, caricamento del kernel, `BootParams` | `stage2/elf.c`, `libs/boot/bootparams.h` |
| 9 | [Interrupt ed eccezioni](09_Interrupt_ed_eccezioni_IDT_e_ISR.md) | IDT, eccezioni, codici di errore, ISR, `Registers`, kernel panic | `kernel/arch/i686/idt.c`, `isr.c`, `isr_asm.asm` |
| 10 | [PIC 8259 e IRQ](10_PIC_8259_e_IRQ.md) | interrupt hardware, rimappatura, ICW1–4, maschera, EOI, esercizio del timer | `i8259.c`, `irq.c` |
| 11 | [Rilevare la memoria](11_Rilevare_la_memoria_E820.md) | perché serve, E820, mappa reale di QEMU | `stage2/memdetect.c`, `x86.asm` |
| 12 | [Il sistema di build](12_Il_sistema_di_build_SCons.md) | SConstruct, ambienti, SConscript, creazione dell'immagine | `SConstruct`, `*/SConscript` |
| 13 | [Debug](13_Debug.md) | sintomi, log E9, `qemu -d int`, GDB, Bochs, metodo | `scripts/debug.sh`, `scripts/bochs.sh` |
| 14 | [Timer, orologio e tastiera](14_Timer_orologio_e_tastiera.md) | PIT, RTC/CMOS, scancode, layout italiano, code page 437, buffer circolare | `pit.c`, `rtc.c`, `keyboard.c` |
| 15 | [Disco ATA e FAT in scrittura](15_Disco_ATA_e_FAT_in_scrittura.md) | ATA PIO, cache, allocare cluster, nomi lunghi in scrittura, verificare con fsck | `ata.c`, `fs/fat.c` |
| 16 | [La shell, l'editor e neofetch](16_La_shell_editor_e_neofetch.md) | REPL, line editor, Tab, parsing, redirezione, percorsi, editor, neofetch, reboot/shutdown | `shell/`, `fs/path.c`, `power.c` |

Il percorso completo, in una riga:

```
BIOS → MBR (0x7C00→0x600) → stage1 (VBR, 0x7C00) → stage2 (0x500, real→protected mode,
FAT, E820, ELF) → kernel (0x100000: GDT, IDT, ISR, PIC, timer, tastiera, disco ATA, FAT)
→ shell "dacos@DACOS:/$"
```

Per la panoramica di tutti i file, le dipendenze e i bug corretti vedi
[`../RELAZIONE.md`](../RELAZIONE.md).
