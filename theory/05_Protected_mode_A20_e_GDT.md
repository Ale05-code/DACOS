# Capitolo 5: protected mode, linea A20 e GDT

## 1. I limiti del real mode

In real mode (quello dell'8086) abbiamo:
- registri e offset a **16 bit** (segmenti da 64 KB);
- solo **1 MB** di memoria;
- **nessuna protezione**: qualsiasi codice può scrivere ovunque, anche sul sistema operativo;
- in compenso possiamo usare i **servizi del BIOS**.

Dal **386** (1985) la CPU ha il **protected mode** (modalità protetta):
- registri e indirizzi a **32 bit** → **4 GB**;
- **livelli di privilegio** (*ring* 0–3): il kernel in ring 0, i programmi in ring 3;
- protezione della memoria (segmentazione con limiti, poi la paginazione);
- **niente BIOS**: i suoi gestori di interrupt sono codice a 16 bit e la tabella degli interrupt
  cambia formato.

Il nostro stage2 passa in protected mode quasi subito e **torna temporaneamente** in real mode ogni
volta che gli serve il BIOS (leggere il disco, chiedere la mappa della memoria).

## 2. La linea A20

L'8086 aveva 20 linee di indirizzo (A0–A19). L'indirizzo più alto, `FFFF:FFFF`, vale

```
0xFFFF × 16 + 0xFFFF = 0x10FFEF
```

cioè **oltre 1 MB**; sull'8086 il bit 20 non esisteva e l'indirizzo "tornava indietro" a
`0x0FFEF` (*wrap-around*). Alcuni programmi dell'epoca ci contavano.

Quando l'IBM AT (286) arrivò con 24 linee, per compatibilità la **ventunesima linea, A20,** fu
collegata a un interruttore, **spento** all'avvio. Con A20 spenta il bit 20 di ogni indirizzo è
forzato a 0, quindi `0x100000` = `0x000000`, `0x300000` = `0x200000`… Uno ogni due megabyte è
inaccessibile. Va accesa.

### Accenderla tramite il controller della tastiera (8042)

Per motivi storici l'interruttore A20 stava nel **controller della tastiera**. È il metodo usato in
`stage2/entry.asm`:

| Porta | Uso |
|---|---|
| `0x64` (scrittura) | comandi al controller |
| `0x64` (lettura) | stato: bit 0 = c'è un dato da leggere, bit 1 = il controller è occupato |
| `0x60` | dati |

```
EnableA20:
    attendi che sia libero ; scrivi 0xAD su 0x64   → disabilita la tastiera
    attendi                ; scrivi 0xD0 su 0x64   → "leggi l'output port"
    attendi un dato        ; leggi 0x60            → valore attuale
    attendi                ; scrivi 0xD1 su 0x64   → "scrivi l'output port"
    attendi                ; scrivi (valore | 2) su 0x60   → bit 1 = A20 accesa
    attendi                ; scrivi 0xAE su 0x64   → riabilita la tastiera
```

`A20WaitInput` aspetta che il bit 1 dello stato sia 0, `A20WaitOutput` che il bit 0 sia 1.

(Esistono altri metodi: il "Fast A20" sulla porta `0x92` e `int 15h, AX=2401h`. Su QEMU A20 è
spesso già accesa.)

## 3. La segmentazione in protected mode: la GDT

In protected mode i registri di segmento non contengono più "segmento × 16": contengono un
**selettore**, cioè un riferimento a una voce di una tabella in memoria, la **GDT** (*Global
Descriptor Table*). Ogni voce, un **descrittore** da 8 byte, descrive un segmento:

```
 63        56 55  52 51   48 47      40 39        16 15         0
┌────────────┬──────┬───────┬──────────┬────────────┬────────────┐
│ base 31-24 │flags │limite │ accesso  │ base 23-0  │ limite 15-0│
│            │ GD0- │ 19-16 │          │            │            │
└────────────┴──────┴───────┴──────────┴────────────┴────────────┘
```

- **base** (32 bit): dove inizia il segmento;
- **limite** (20 bit): quanto è lungo;
- **flag**:
  - **G** (granularità): 0 = limite in byte (max 1 MB), 1 = limite in **pagine da 4 KB**
    (0xFFFFF × 4 KB = **4 GB**);
  - **D** (dimensione): 0 = segmento a 16 bit, 1 = a **32 bit**;
- **byte di accesso**:

| Bit | 7 | 6–5 | 4 | 3 | 2 | 1 | 0 |
|---|---|---|---|---|---|---|---|
| Nome | **P** presente | **DPL** ring | **S** codice/dati | **E** eseguibile | DC direzione/conforming | **RW** leggibile/scrivibile | A acceduto |

### Esempio: decodificare il descrittore del codice di `entry.asm`

```nasm
dw 0FFFFh       ; limite 0-15 = 0xFFFF
dw 0            ; base 0-15 = 0
db 0            ; base 16-23 = 0
db 10011010b    ; accesso
db 11001111b    ; flag (1100) + limite 16-19 (1111)
db 0            ; base 24-31 = 0
```

- accesso `1001 1010`: P=1 (presente), DPL=00 (**ring 0**), S=1 (codice/dati), E=1 (**codice**),
  DC=0, RW=1 (leggibile), A=0;
- flag `1100`: G=1 (pagine da 4 KB), D=1 (**32 bit**);
- base 0, limite 0xFFFFF × 4 KB = **4 GB**.

Il descrittore dei dati è identico tranne il byte di accesso `10010010b` (E=0 = dati, RW=1 =
scrivibile).

### La GDT dello stage2

| Selettore | Indice | Segmento |
|---|---|---|
| `0x00` | 0 | **NULL** (obbligatorio, tutto a zero) |
| `0x08` | 1 | codice 32 bit, base 0, 4 GB |
| `0x10` | 2 | dati 32 bit, base 0, 4 GB |
| `0x18` | 3 | codice **16 bit**, base 0, 1 MB (serve per tornare in real mode) |
| `0x20` | 4 | dati 16 bit |

Il selettore è `indice × 8` (i 3 bit bassi indicano tabella e livello di privilegio richiesto).

Tutti i segmenti hanno base 0 e coprono tutta la memoria: è il **flat memory model**. In pratica la
segmentazione è "spenta" e un puntatore C è direttamente l'indirizzo fisico. La protezione vera si
farà più avanti con la **paginazione**.

### Caricare la GDT: `lgdt`

`lgdt` vuole l'indirizzo di un piccolo **descrittore della tabella**:

```nasm
g_GDTDesc:  dw g_GDTDesc - g_GDT - 1    ; dimensione - 1
            dd g_GDT                    ; indirizzo lineare
```

## 4. Entrare in protected mode (`stage2/entry.asm`)

```nasm
cli                         ; 1. niente interrupt: la IVT del BIOS non vale più
call EnableA20              ; 2. A20
call LoadGDT                ; 3. lgdt
mov eax, cr0                ; 4. bit PE (Protection Enable) di CR0
or al, 1
mov cr0, eax
jmp dword 08h:.pmode        ; 5. far jump: CS = 0x08 e svuota la pipeline
.pmode:
[bits 32]
mov ax, 0x10                ; 6. tutti i segmenti dati = 0x10
mov ds, ax
mov es, ax
mov fs, ax
mov gs, ax
mov ss, ax
```

Perché il **far jump**? Dopo aver acceso PE la CPU sta ancora eseguendo con il vecchio `CS` (del
real mode) e ha già decodificato le istruzioni successive come codice a 16 bit. Il salto "far"
ricarica `CS` con un selettore valido e fa ripartire la decodifica in 32 bit.

> **Bug corretto**: l'originale caricava solo `DS` e `SS`. Subito dopo, `rep stosb` (che azzera la
> `.bss`) scrive usando **`ES`**, che conteneva ancora il valore del real mode. Funzionava solo
> perché la CPU tiene in una cache nascosta base 0 e limite 64 KB. Ora vengono caricati tutti.

## 5. Tornare in real mode (macro `x86_EnterRealMode` in `x86.asm`)

```nasm
[bits 32]
jmp word 18h:.pmode16       ; 1. salta al segmento codice a 16 bit
.pmode16:
[bits 16]
mov eax, cr0                ; 2. spegni PE
and al, ~1
mov cr0, eax
jmp word 00h:.rmode         ; 3. far jump in real mode (CS = 0)
.rmode:
mov ax, 0                   ; 4. segmenti del real mode
mov ds, ax
mov ss, ax
sti                         ; 5. il BIOS ha bisogno degli interrupt
```

Non si può spegnere PE mentre si esegue codice a 32 bit: prima si passa al segmento di codice
**a 16 bit** (`0x18`), ancora in protected mode, poi si spegne PE, poi un far jump carica un `CS`
"da real mode".

### Lo schema di ogni chiamata al BIOS dallo stage2

```
codice C (32 bit)
   └─ x86_Disk_Read(...)          funzione assembly, convenzione cdecl
        push ebp / mov ebp, esp
        x86_EnterRealMode         → ora siamo a 16 bit
        prepara i registri leggendo gli argomenti da [bp + 8], [bp + 12]...
        int 13h
        mov eax, 1 / sbb eax, 0   → risultato 1 = ok, 0 = errore (dal carry)
        x86_EnterProtectedMode    → di nuovo a 32 bit
        mov esp, ebp / pop ebp / ret
```

Funziona solo perché **lo stack sta sotto i 64 KB** (`SP = 0xFFF0`): in real mode `SS:BP` con
`SS = 0` raggiunge solo i primi 64 KB. Per lo stesso motivo i **buffer** passati al BIOS devono
stare **sotto 1 MB**, e vengono convertiti in `segmento:offset` con la macro `LinearToSegOffset`:

```
segmento = indirizzo >> 4        offset = indirizzo & 0xF
```

Esempio: buffer `0x20000` → `2000:0000`.

## 6. La GDT del kernel

Il kernel non si fida della GDT dello stage2, che sta in memoria bassa e potrebbe essere
sovrascritta. Ne crea una sua in C (`kernel/arch/i686/gdt.c`, con la macro `GDT_ENTRY`) e la carica
con `i686_GDT_Load` (`gdt_asm.asm`):

```nasm
lgdt [eax]
push 0x08               ; nuovo CS
push .reload_cs         ; indirizzo di ritorno
retf                    ; "far return": carica CS:EIP dallo stack
.reload_cs:
mov ax, 0x10            ; tutti i segmenti dati
mov ds, ax  ...  mov ss, ax
```

Il `retf` è un modo comodo per fare un far jump a un selettore che arriva come parametro.

## 7. Riassunto

| | Real mode | Protected mode |
|---|---|---|
| Bit | 16 | 32 |
| Memoria | 1 MB | 4 GB |
| Registri di segmento | segmento × 16 | selettore → GDT |
| Interrupt | IVT a `0x0000`, BIOS disponibile | IDT (capitolo 9), niente BIOS |
| Protezione | nessuna | ring 0–3, limiti, paginazione |
