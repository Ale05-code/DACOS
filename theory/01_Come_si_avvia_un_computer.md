# Capitolo 1: come si avvia un computer (e il primo codice)

## 1. Dall'accensione al nostro codice

Quando premi il tasto di accensione succede questo:

1. **Reset della CPU.** Il processore parte in uno stato fisso: modalità a **16 bit** (*real mode*),
   ed esegue l'istruzione all'indirizzo `0xFFFFFFF0`, il **reset vector**. Lì c'è il firmware
   della scheda madre: il **BIOS** (*Basic Input/Output System*).
2. **POST** (*Power-On Self Test*): il BIOS controlla la RAM, inizializza la scheda video, la
   tastiera, i dischi, e mostra il logo.
3. **Ricerca del dispositivo di boot**, nell'ordine impostato (floppy, disco, USB, CD…).
4. Per ogni dispositivo legge il **primo settore** (512 byte) e controlla se termina con la **firma
   di boot**: i byte `0x55 0xAA`.
5. Se la firma c'è, copia quel settore in RAM all'indirizzo **`0x7C00`**, mette in **`DL` il numero
   del disco** (`0x00` = primo floppy, `0x80` = primo hard disk) e **salta** a `0x7C00`.

Da quel momento la CPU esegue il **nostro** codice. Non c'è nessun sistema operativo, nessuna
libreria, niente: solo la CPU, la RAM e i servizi del BIOS.

### Legacy BIOS e UEFI

Ci sono due modi in cui il firmware avvia un sistema:

- **Legacy (BIOS)**: quello appena descritto. È quello che usiamo.
- **UEFI**: il firmware moderno cerca una partizione speciale (EFI System Partition, in FAT) e
  carica un file `.efi`, già in modalità a 32/64 bit. È più comodo ma molto più complesso da
  spiegare; quasi tutti i PC moderni hanno comunque una modalità di compatibilità legacy (CSM).
  QEMU usa di default **SeaBIOS**, un BIOS legacy.

## 2. Cos'è l'assembly

- Il processore capisce solo il **codice macchina**: sequenze di byte. Per esempio `B8 05 00`
  significa "metti 5 nel registro AX".
- L'**assembly** è la versione leggibile: `mov ax, 5`. Ogni istruzione ha un **mnemonico** (`mov`)
  e da zero a due (raramente tre) **operandi**.
- L'**assembler** (NASM) traduce l'assembly in codice macchina quasi uno a uno. Un compilatore C fa
  molto di più (analisi, ottimizzazioni…).
- La prima parte di un sistema operativo **deve** essere in assembly: non c'è ancora uno stack, i
  registri di segmento non sono impostati, bisogna parlare direttamente con l'hardware.

### Architetture

- Ogni famiglia di processori ha le sue istruzioni: **x86** (PC), **ARM** (telefoni, Mac M1…),
  RISC-V…
- L'x86 è **retrocompatibile** fino all'**8086** del 1978: un PC moderno, appena acceso, si comporta
  come un 8086. Per questo partiamo a 16 bit.

## 3. Direttive e istruzioni

- Un'**istruzione** diventa codice macchina (`mov`, `jmp`, `int`…).
- Una **direttiva** dà indicazioni all'assembler e non produce codice:
  - `bits 16`: genera codice a 16 bit (**non** cambia la modalità della CPU, dice solo ad NASM
    come codificare le istruzioni);
  - `org 0x7C00`: "questo codice verrà eseguito all'indirizzo 0x7C00", quindi calcola le etichette
    a partire da lì. Nel progetto attuale la stessa cosa la fa il **linker script**
    (`. = 0x7C00;` in `stage1/linker.ld`), mentre l'MBR, che è un binario semplice, usa `org 0x0600`;
  - `db`, `dw`, `dd`, `dq`: scrivono byte (1), word (2), double word (4), quad word (8);
  - `times N istruzione`: ripete N volte (per esempio `times 440 - ($ - $$) db 0` riempie di zeri);
  - `$` = indirizzo della riga corrente, `$$` = inizio della sezione, quindi `$ - $$` = quanti byte
    abbiamo scritto finora.

## 4. Il primo programma: un settore di boot

Il programma più piccolo possibile:

```nasm
bits 16
org 0x7C00

main:
    hlt                 ; ferma la CPU
.halt:
    jmp .halt           ; se si risveglia (per un interrupt), torna a fermarsi

times 510 - ($ - $$) db 0   ; riempie fino al byte 510
dw 0xAA55                   ; firma di boot (in memoria: 55 AA, perché x86 è little-endian)
```

- **512 byte esatti**: 510 di codice/dati + 2 di firma.
- **Little-endian**: un numero di più byte si scrive in memoria partendo dal byte **meno
  significativo**. `dw 0xAA55` diventa i byte `55 AA`, che è ciò che il BIOS cerca.
- Nel progetto vero lo stesso controllo è nel linker script dello stage1:

  ```
  .bios_footer 0x7DFE : { SHORT(0xAA55) }
  ```

  `0x7DFE` = `0x7C00 + 510`. Se il codice diventa troppo lungo e "invade" quei due byte, il linker
  dà errore (`section .bios_footer overlaps section .rodata`). L'ho verificato.

## 5. Registri x86

I **registri** sono piccolissime memorie dentro la CPU, velocissime.

| Tipo | Registri (16 bit / 32 bit) | Uso |
|---|---|---|
| Generali | `AX BX CX DX` / `EAX EBX ECX EDX` | conti, parametri. `AX` si divide in `AH` (alto) e `AL` (basso) |
| Indice | `SI DI` / `ESI EDI` | puntatori (*source index*, *destination index*) |
| Stack | `SP BP` / `ESP EBP` | cima dello stack, base del "frame" della funzione |
| Program counter | `IP` / `EIP` | indirizzo della prossima istruzione |
| Segmento | `CS DS ES FS GS SS` | quali segmenti di memoria sono attivi |
| Flag | `FLAGS` / `EFLAGS` | risultati dell'ultima operazione: Zero (ZF), Carry (CF), Sign (SF)… e Interrupt (IF) |
| Controllo | `CR0 CR2 CR3 CR4` | configurano la CPU (`CR0` bit 0 = protected mode) |

## 6. La memoria in real mode: segmentazione

- L'8086 aveva un bus di indirizzi a **20 bit**, cioè 2²⁰ = **1 MB** indirizzabile, ma registri a
  **16 bit** (max 64 KB). Intel ha risolto con la **segmentazione**:

  ```
  indirizzo fisico = segmento × 16 + offset          (segmento:offset)
  ```

- Ogni segmento copre 64 KB e i segmenti si **sovrappongono** ogni 16 byte, quindi lo stesso
  indirizzo si scrive in tanti modi:
  - `0x0000:0x7C00` → 0 × 16 + 0x7C00 = **0x7C00**
  - `0x07C0:0x0000` → 0x7C00 + 0 = **0x7C00**
  - `0x0700:0x0C00` → 0x7000 + 0xC00 = **0x7C00**
- Il codice usa `CS:IP`; i dati usano `DS` (o `ES` per alcune istruzioni); lo stack usa `SS:SP`.
- **CS si può cambiare solo con un salto** "far" (`jmp segmento:offset`, `retf`). Il nostro stage1
  lo fa subito:

  ```nasm
  push es             ; es = 0
  push word .after
  retf                ; "ritorna" a 0000:.after → CS = 0
  ```

  perché alcuni BIOS saltano a `07C0:0000` invece che a `0000:7C00`. L'indirizzo fisico è lo stesso,
  ma le etichette (calcolate rispetto a 0x7C00) funzionano solo se `CS = 0`.

### Riferimenti alla memoria

- Sintassi NASM: `[segmento:offset]`; se il segmento manca si usa `DS` (o `SS` se c'è `BP`).
- In 16 bit le combinazioni permesse sono limitate: base = `BX` o `BP`, indice = `SI` o `DI`, più
  una costante. In 32 bit qualsiasi registro, più un fattore di scala 1/2/4/8:
  `[ebx + esi*4 + 8]`.
- **Non si può** scrivere una costante direttamente in un registro di segmento:

  ```nasm
  mov ax, 0
  mov ds, ax          ; giusto
  ; mov ds, 0         ; errore!
  ```

## 7. Lo stack

- È una zona di memoria usata come una **pila** (LIFO, *Last In First Out*): `push` mette, `pop`
  toglie.
- **Cresce verso il basso**: `push` prima **decrementa** `SP` e poi scrive.
- `call funzione` fa `push` dell'indirizzo di ritorno e salta; `ret` fa `pop` e ci torna.
- Nello stage1: `SS = 0`, `SP = 0x7C00`. Lo stack parte **sotto** il nostro codice e scende verso
  0x500, quindi non lo sovrascrive mai.

## 8. Chiamare il BIOS: gli interrupt

Un **interrupt** è un segnale che fa interrompere alla CPU ciò che sta facendo per eseguire una
routine apposita (il *gestore*). Può arrivare da:

1. un'**eccezione** della CPU (divisione per zero, istruzione non valida…);
2. l'**hardware** (un tasto premuto, il disco che ha finito di leggere);
3. il **software**, con l'istruzione `int N` (N da 0 a 255).

Il BIOS installa dei gestori per un gruppo di `int` che sono i suoi "servizi". Di solito il numero di
interrupt sceglie la categoria e il registro `AH` la funzione:

| Interrupt | Categoria | Funzioni che usiamo |
|---|---|---|
| `int 0x10` | video | `AH=0x0E`: stampa il carattere in `AL` (modalità *teletype*) |
| `int 0x13` | dischi | `AH=0x00` reset, `0x02` lettura CHS, `0x08` geometria, `0x41`/`0x42` LBA |
| `int 0x15` | sistema | `EAX=0xE820` mappa della memoria |
| `int 0x16` | tastiera | `AH=0x00` aspetta un tasto |

## 9. Stampare una stringa

La funzione `puts` dello stage1 (`src/bootloader/stage1/boot.asm`):

```nasm
; stampa la stringa terminata da 0 puntata da DS:SI
puts:
    push si             ; salva i registri che modificheremo
    push ax
    push bx
.loop:
    lodsb               ; AL = [DS:SI], poi SI = SI + 1
    or al, al           ; AL | AL non cambia AL ma imposta lo Zero Flag se AL == 0
    jz .done            ; fine stringa
    mov ah, 0x0E        ; funzione "teletype"
    mov bh, 0           ; pagina video 0
    int 0x10
    jmp .loop
.done:
    pop bx              ; ripristina in ordine inverso
    pop ax
    pop si
    ret
```

- Le stringhe sono terminate da un byte **0** (come in C).
- Per andare a capo servono **due** caratteri: `0x0D` (*carriage return*, torna a inizio riga) e
  `0x0A` (*line feed*, scende di una riga). Il codice usa la macro `%define ENDL 0x0D, 0x0A`.
- Le etichette che iniziano con `.` (come `.loop`) sono **locali**: valgono solo dentro la funzione
  sopra di loro, quindi ogni funzione può avere il suo `.loop`.

## 10. Dove si vede tutto questo nel progetto

- `src/bootloader/mbr/mbr.asm`: primo settore del disco (lo carica il BIOS a 0x7C00).
- `src/bootloader/stage1/boot.asm`: primo settore della partizione, o del floppy.
- Prova: `scons imageType=floppy && scons run imageType=floppy`.
