# Capitolo 11: rilevare la memoria (E820)

## 1. Perché il kernel non sa quanta RAM c'è

Sembrerebbe facile: "ho 32 MB, uso da 0 a 32 MB". Invece la memoria fisica è piena di **buchi** e
zone **riservate**:
- i primi 640 KB sono RAM, ma dentro ci sono IVT, BDA, EBDA;
- da 640 KB a 1 MB ci sono la memoria video e le ROM;
- più in alto ci possono essere zone usate dall'ACPI, dal firmware, da dispositivi mappati in memoria
  (PCI), RAM guasta…
- in cima ai 4 GB c'è di nuovo il BIOS (`0xFFFC0000`).

Se il kernel ci scrivesse a caso, distruggerebbe dati del firmware o di qualche dispositivo. Bisogna
**chiedere al BIOS la mappa**, e va fatto **prima** di lasciare definitivamente il real mode: è uno
dei compiti principali del bootloader.

## 2. `int 0x15`, `EAX = 0xE820`

È la funzione standard (dal 1990 circa) per la mappa della memoria. Restituisce **una regione per
chiamata**:

| Ingresso | |
|---|---|
| `EAX` | `0xE820` |
| `EDX` | `0x534D4150` = **'SMAP'** (firma) |
| `ECX` | dimensione del buffer (24) |
| `ES:DI` | buffer dove scrivere la regione |
| `EBX` | **valore di continuazione**: 0 alla prima chiamata, poi quello restituito |

| Uscita | |
|---|---|
| Carry = 0 | ok |
| `EAX` | di nuovo `'SMAP'` (se no, la funzione non è supportata) |
| `ECX` | byte scritti (20 o 24) |
| `EBX` | continuazione per la chiamata successiva; **0 = quella appena restituita era l'ultima** |

La regione (struttura `E820MemoryBlock`):

```c
typedef struct {
    uint64_t Base;      // indirizzo di inizio
    uint64_t Length;    // lunghezza
    uint32_t Type;      // 1 usabile, 2 riservata, 3 ACPI recuperabile, 4 ACPI NVS, 5 guasta
    uint32_t ACPI;      // attributi estesi (ACPI 3.0), se ECX = 24
} E820MemoryBlock;
```

## 3. Il codice

Assembly (`stage2/x86.asm`, `x86_E820GetNextBlock`): torna in real mode, converte i puntatori in
segmento:offset, chiama `int 0x15`, controlla la firma, salva il nuovo `EBX` e ritorna in protected
mode.

C (`stage2/memdetect.c`, versione corretta):

```c
uint32_t continuation = 0;
block.ACPI = 1;                     // valido, se il BIOS restituisce solo 20 byte
ret = x86_E820GetNextBlock(&block, &continuation);

while (ret > 0 && g_MemRegionCount < MAX_REGIONS)
{
    salva block in g_MemRegions[g_MemRegionCount++];
    if (continuation == 0)          // era l'ultima
        break;
    block.ACPI = 1;
    ret = x86_E820GetNextBlock(&block, &continuation);
}
```

## 4. Esempio reale: QEMU con `-m 32` (32 MB)

Output vero dello stage2:

```
E820: base=0x0        length=0x9fc00   type=0x1
E820: base=0x9fc00    length=0x400     type=0x2
E820: base=0xf0000    length=0x10000   type=0x2
E820: base=0x100000   length=0x1ee0000 type=0x1
E820: base=0x1fe0000  length=0x20000   type=0x2
E820: base=0xfffc0000 length=0x40000   type=0x2
```

| Da | A | Dimensione | Tipo | Cos'è |
|---|---|---|---|---|
| `0x00000000` | `0x0009FBFF` | 639 KB | usabile | memoria "convenzionale" (contiene però IVT e BDA) |
| `0x0009FC00` | `0x0009FFFF` | 1 KB | riservata | **EBDA** (Extended BIOS Data Area) |
| (`0xA0000`–`0xEFFFF`) | | | *non elencata* | memoria video e ROM: un buco, quindi da non usare |
| `0x000F0000` | `0x000FFFFF` | 64 KB | riservata | **ROM del BIOS** |
| `0x00100000` | `0x01FDFFFF` | **30,9 MB** | usabile | la RAM vera, sopra 1 MB: qui c'è il kernel |
| `0x01FE0000` | `0x01FFFFFF` | 128 KB | riservata | tabelle ACPI e dati del firmware |
| `0xFFFC0000` | `0xFFFFFFFF` | 256 KB | riservata | copia del BIOS in cima ai 4 GB (dove la CPU parte, capitolo 1) |

Con Bochs (128 MB) la mappa è diversa (`0x9f000`, `0xe8000`, `0x100000` per 127 MB…): ogni macchina
ha la sua, per questo va chiesta.

Somma della RAM usabile: 0x9FC00 + 0x1EE0000 ≈ 31,5 MB su 32 MB.

## 5. A cosa servirà

Il kernel riceve la mappa in `BootParams.Memory` (capitolo 8) e per ora la stampa e basta. Il passo
successivo, nelle puntate seguenti, è un **allocatore di memoria fisica**: una struttura (per esempio
una bitmap con un bit per ogni pagina da 4 KB) che sa quali pagine sono libere, inizializzata
segnando come libere **solo** le regioni di tipo 1 e togliendo quelle occupate dal kernel e dal
bootloader. Da lì vengono `malloc`, la paginazione e i processi.

## 6. Altri metodi (per cultura)

- `int 0x12`: memoria convenzionale in KB (max 640);
- `int 0x15, AX = 0xE801` o `AH = 0x88`: memoria estesa, metodi più vecchi e limitati;
- su UEFI si usa `GetMemoryMap()`.

E820 è l'unico affidabile su tutti i BIOS moderni.
