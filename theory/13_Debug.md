# Capitolo 13: come si fa debug di un sistema operativo

Quando un programma normale va in crash, il sistema operativo te lo dice. Quando va in crash **il
sistema operativo**, di solito vedi solo lo schermo nero o il PC che si riavvia. Servono altri
strumenti.

## 1. I sintomi più comuni

| Sintomo | Causa probabile |
|---|---|
| Si riavvia di continuo | **triple fault**: un'eccezione senza gestore (prima della IDT, o con la IDT rotta) |
| Schermo nero, nessun messaggio | si è fermato prima di stampare (stage1/stage2), o un ciclo infinito |
| `Read failed!` | stage1: lettura dello stage2 fallita |
| `MBR: ...` | il nostro MBR non trova o non legge la partizione |
| `FAT: ... not found`, `ELF: can't open` | stage2: kernel non trovato nell'immagine |
| `KERNEL PANIC!` + registri | eccezione nel kernel: c'è tutto quello che serve (capitolo 9) |

## 2. Primo strumento: i log su E9

```bash
scons run
```

Tutto quello che lo stage2 stampa e i `log_*` del kernel compaiono nel terminale. Aggiungere un
`log_debug("Modulo", "x = %x", x);` è il modo più veloce per capire fin dove arriva il codice.

## 3. QEMU che racconta cosa succede

```bash
qemu-system-i386 -m 32 -debugcon stdio \
    -drive file=build/i686_debug/image.img,format=raw,index=0,media=disk \
    -d int,cpu_reset -D qemu.log -no-reboot
```

- `-d int`: scrive in `qemu.log` **ogni interrupt ed eccezione** con lo stato dei registri;
- `-d cpu_reset`: anche i reset (un triple fault diventa un reset);
- `-no-reboot`: invece di riavviarsi, QEMU si chiude, così l'ultimo stato resta nel log.

Esempio vero, il kernel da 216 KB con il vecchio `memcpy` a 16 bit:

```
check_exception old: 0xffffffff new 0xd
     0: v=0d e=0000 i=0 cpl=0 IP=0008:001149fd ... EAX=001001cf
check_exception old: 0xd new 0xd
```

`v=0d` = eccezione 13 (General Protection Fault) a `EIP=0x1149fd`, poi un'altra 13 mentre gestiva la
prima → double fault → triple fault. `EAX=0x1001cf` era l'entry point del kernel: lo stage2 ci era
saltato, ma lì non c'era il codice, perché `memcpy` non l'aveva copiato.

Altri comandi utili, dal **monitor** di QEMU (Ctrl+Alt+2 nella finestra, o `-monitor stdio`):
- `info registers`: registri;
- `x /16xb 0x7c00`: 16 byte di memoria;
- `info mem`, `info tlb`: paginazione (quando ci sarà).

## 4. GDB collegato a QEMU

```bash
scons debug
```

`scripts/debug.sh` avvia `gdb` con questo script (`.vscode/.gdb_script.gdb`):

```
symbol-file build/i686_debug/kernel/kernel.elf     # simboli: nomi delle funzioni, righe di C
set disassembly-flavor intel
target remote | qemu-system-i386 -S -gdb stdio -m 32 -drive ...
```

`-S` fa partire QEMU **in pausa**, aspettando GDB. Comandi base:

| Comando | Cosa fa |
|---|---|
| `b start` | breakpoint sulla funzione `start` del kernel |
| `b *0x7c00` | breakpoint su un indirizzo (stage1) |
| `c` | continua |
| `n` / `s` | riga successiva / entra nella funzione |
| `si` | una sola istruzione assembly |
| `bt` | backtrace: chi ha chiamato chi |
| `p bootParams->Memory.RegionCount` | stampa una variabile C |
| `info registers` | registri |
| `x/10i $eip` | disassembla 10 istruzioni |
| `layout asm` / `layout src` | interfaccia testuale con il codice |

Verificato sul progetto:

```
Breakpoint 1, start (bootParams=0x36b8) at src/kernel/main.c:19
#0  start (bootParams=0x36b8) at src/kernel/main.c:19
#1  0x00102a71 in entry () at src/kernel/entry.asm:20
```

Per il codice a **16 bit** (MBR, stage1, inizio dello stage2) GDB fa fatica: `set architecture
i8086` aiuta, ma per questa parte è meglio Bochs. Lo stage2 è un binario grezzo senza simboli: per
sapere a che indirizzo sta una funzione si guarda la **mappa** `build/i686_debug/stage2/stage2.map`
e si mette un breakpoint su quell'indirizzo (`b *0x...`).

## 5. Bochs

```bash
scons bochs
```

Si apre la finestra del debugger grafico (`gui_debug`): registri, disassembly e memoria sempre
visibili, anche in real mode. Comandi del prompt:

| Comando | Cosa fa |
|---|---|
| `b 0x7c00` | breakpoint (indirizzo fisico) |
| `c` | continua |
| `s` | un'istruzione |
| `r` | registri generali |
| `sreg` | registri di segmento (con base e limite nascosti!) |
| `info gdt`, `info idt` | mostra GDT e IDT decodificate |
| `x /16bx 0x7c00` | memoria |

Bochs è più **severo** di QEMU, ed è utile proprio per questo: è stato Bochs a mostrare che l'MBR di
libparted non funzionava con una geometria diversa (capitolo 4). Se il sistema funziona in QEMU **e**
in Bochs, è molto probabile che funzioni anche su un PC vero.

## 6. Strumenti per guardare i file compilati

```bash
TC=../.toolchains/i686-elf/bin
xxd build/i686_debug/stage1_fat32/stage1.bin | less          # byte per byte
ndisasm -b 16 build/i686_debug/mbr/mbr.bin | less            # disassembla codice a 16 bit
$TC/i686-elf-objdump -d -M intel build/i686_debug/kernel/kernel.elf | less
$TC/i686-elf-addr2line -f -e build/i686_debug/kernel/kernel.elf 0x102aea   # indirizzo → funzione e riga
grep -n start build/i686_debug/kernel/kernel.map
```

Esempio: dopo il KERNEL PANIC di `crash_me` (`eip=102aea`), `addr2line` risponde
`crash_me … io_asm.asm:39`.

## 7. Un metodo

1. **Riproduci** il problema nel modo più semplice (floppy, kernel minimo…).
2. **Restringi**: fin dove arriva? Aggiungi log o breakpoint a metà strada (ricerca binaria).
3. **Guarda lo stato** (registri, memoria) al momento dell'errore: `-d int`, GDB, Bochs.
4. **Fai un'ipotesi** e verificala con un esperimento piccolo. È così che ho trovato i bug di questo
   progetto: per esempio "se `memcpy` tronca a 16 bit, un kernel da più di 64 KB non partirà" → ho
   aggiunto 200 KB di dati al kernel → non partiva → con la correzione sì.
