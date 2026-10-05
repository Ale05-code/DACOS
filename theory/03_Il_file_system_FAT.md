# Capitolo 3: il file system FAT

## 1. Cos'è un file system

Un **file system** è un modo di **organizzare i dati** su un disco. Senza, il disco sarebbe solo
una lunga fila di settori e non sapremmo dove inizia e finisce un file, né come si chiama.

Di solito organizza i dati in **file** e **cartelle** annidate, e tiene per ogni file nome,
dimensione, date e posizione sul disco.

## 2. I file system più comuni

- **NTFS**: Windows. Permessi, journaling, compressione, cifratura… complesso.
- **FAT** (FAT12, FAT16, FAT32) ed **exFAT**: MS-DOS e Windows 9x; oggi su chiavette USB e schede
  SD perché li leggono tutti. **Semplicissimo** da implementare.
- **ext2/3/4**: Linux.
- **APFS** (e il vecchio HFS+): macOS.

Scegliamo **FAT**, perché è semplice e "lo supportano anche i tostapane".

## 3. Le quattro regioni di un volume FAT

```
┌──────────────────┬──────────┬──────────┬────────────────┬───────────────────────────┐
│ Settori riservati│  FAT n.1 │  FAT n.2 │ Root directory │      Regione dati         │
│ (boot sector,    │          │ (copia)  │ (solo FAT12/16)│  cluster 2, 3, 4, 5, …    │
│  stage2…)        │          │          │                │                           │
└──────────────────┴──────────┴──────────┴────────────────┴───────────────────────────┘
```

1. **Settori riservati**: iniziano con il **boot sector**, che contiene i parametri del volume.
   Nel nostro floppy qui c'è **anche lo stage2** (per questo i settori riservati sono 26 e non 1).
   In FAT32 qui c'è anche il settore **FSInfo**.
2. **FAT** (*File Allocation Table*): una tabella che, per ogni blocco di dati, dice qual è il
   blocco **successivo** del file. Ce ne sono due copie, per sicurezza.
3. **Root directory**: l'"indice" della cartella principale. Ogni file o cartella ha una voce da
   32 byte. In FAT32 non esiste come regione fissa: la root è una cartella normale nella regione
   dati.
4. **Regione dati**: il contenuto dei file e di tutte le altre cartelle.

## 4. Il boot sector: BPB ed EBR

I primi byte del boot sector:

| Offset | Dim. | Campo | Nel nostro floppy |
|---|---|---|---|
| 0 | 3 | salto al codice (`jmp short start` + `nop`) | `EB xx 90` |
| 3 | 8 | OEM | `mkfs.fat` |
| 11 | 2 | **byte per settore** | 512 |
| 13 | 1 | **settori per cluster** | 1 |
| 14 | 2 | **settori riservati** | **26** (1 boot + 25 di stage2) |
| 16 | 1 | **numero di FAT** | 2 |
| 17 | 2 | **voci della root** | 224 |
| 19 | 2 | settori totali (16 bit) | 2880 |
| 21 | 1 | tipo di supporto | `0xF0` = floppy 3,5" |
| 22 | 2 | **settori per FAT** | 9 |
| 24 | 2 | settori per traccia | 18 |
| 26 | 2 | testine | 2 |
| 28 | 4 | settori nascosti | 0 |
| 32 | 4 | settori totali (32 bit, se quello a 16 bit è 0) | 0 |

Questa parte si chiama **BPB** (*BIOS Parameter Block*). Subito dopo c'è l'**EBR** (*Extended Boot
Record*): numero del drive, firma `0x29`, numero di serie, **etichetta** (11 byte, `DACOS`), tipo
(8 byte, `FAT12   `). In FAT32, prima dell'EBR ci sono 28 byte in più: settori per FAT a 32 bit,
**cluster della root**, settore FSInfo, settore di backup, ecc.

> I valori nella colonna di destra li ho letti dall'immagine `image.img` compilata. Il BPB scritto
> in `boot.asm` è solo un segnaposto: lo script di build (`install_stage1`) **conserva il BPB
> creato da `mkfs.fat`** e sostituisce solo il salto e il codice.

## 5. Esempio svolto: trovare `/boot/kernel.elf` nel nostro floppy

### Passo 1: dove inizia la root directory

La root è la terza regione, quindi basta sommare le prime due:

- regione riservata = **26** settori;
- regione FAT = numero di FAT × settori per FAT = 2 × 9 = **18** settori;
- **inizio root = 26 + 18 = LBA 44**.

### Passo 2: quanto è grande la root

- 224 voci × 32 byte = 7168 byte;
- 7168 / 512 = **14 settori**;
- **inizio regione dati = 44 + 14 = LBA 58**.

> **Arrotondare per eccesso** in aritmetica intera: `(byte + 511) / 512`. Con 225 voci
> (7200 byte) servirebbero 14,06 settori, cioè 15. Il codice fa proprio
> `(rootDirSize + BytesPerSector - 1) / BytesPerSector`.

### Passo 3: leggere le voci della root (32 byte ciascuna)

| Offset | Dim. | Campo |
|---|---|---|
| 0 | 11 | nome **8.3**: 8 caratteri di nome + 3 di estensione, maiuscoli, riempiti di spazi |
| 11 | 1 | attributi: `0x01` sola lettura, `0x02` nascosto, `0x04` sistema, `0x08` etichetta del volume, `0x10` **cartella**, `0x20` archivio, `0x0F` = voce di nome lungo (LFN) |
| 12–19 | | date e ore di creazione e accesso |
| 20 | 2 | 16 bit **alti** del primo cluster (solo FAT32) |
| 22–25 | | data e ora di modifica |
| 26 | 2 | 16 bit **bassi** del primo cluster |
| 28 | 4 | dimensione in byte (0 per le cartelle) |

Nella root del nostro floppy ci sono queste voci (lette davvero dall'immagine):

| Nome (11 byte) | Attributi | Primo cluster | Dimensione |
|---|---|---|---|
| `DACOS      ` | `0x08` (etichetta del volume) | 0 | 0 |
| `BOOT       ` | `0x10` (cartella) | **2** | 0 |
| `TEST    TXT` | `0x20` (file) | 408 | 36 |
| `FOLDER     ` | `0x10` (cartella) | 409 | 0 |

Il percorso `/boot/kernel.elf` si divide in `boot` e `kernel.elf`. Il nome `boot` diventa
`"BOOT       "` (funzione `FAT_GetShortName`): lo troviamo, è una cartella, e inizia al **cluster 2**.

### Passo 4: dal cluster al settore

I dati sono allocati in **cluster** (gruppi di settori; qui 1 cluster = 1 settore). I cluster sono
numerati da **2** (0 e 1 sono riservati):

```
LBA = inizio_regione_dati + (cluster − 2) × settori_per_cluster
```

Cluster 2 → LBA = 58 + (2 − 2) × 1 = **58**. Leggiamo il settore 58: è la cartella `boot`, che
contiene `.`, `..` e:

| Nome | Attributi | Primo cluster | Dimensione |
|---|---|---|---|
| `KERNEL  ELF` | `0x20` | **3** | 207 152 byte |

Primo cluster del kernel: **3** → LBA 58 + 1 = **59**.

### Passo 5: seguire la catena nella FAT

Il file è lungo 207 152 byte = 405 cluster, quindi dobbiamo sapere quali cluster seguono il 3. La
**FAT** è un array: all'indice N c'è il **numero del cluster che segue N** (oppure un valore di
"fine file").

In **FAT12** ogni voce occupa **12 bit = 1,5 byte**, quindi due voci stanno in 3 byte:

```
offset della voce N = N × 3 / 2      (divisione intera)
se N è pari:   valore = word(offset) & 0x0FFF
se N è dispari: valore = word(offset) >> 4
```

I primi byte della FAT del nostro floppy sono `f0 ff ff ff 4f 00 05 60 00 07 80 00`.

- **Cluster 3** (dispari): offset 3 × 3 / 2 = 4 → byte `4f 00` → word `0x004F` (little-endian)
  → `0x004F >> 4` = **4**. Il cluster successivo è il 4.
- **Cluster 2** (pari, la cartella `boot`): offset 3 → byte `ff 4f` → word `0x4FFF` →
  `& 0x0FFF` = **`0xFFF`** = **fine catena**: la cartella occupa un solo cluster.

Seguendo la catena: 3 → 4 → 5 → 6 → … → 407 → fine. Il file è contiguo perché l'immagine è appena
stata creata, ma il driver non lo dà per scontato.

### Valori speciali

| FAT12 | FAT16 | FAT32 | Significato |
|---|---|---|---|
| `0x000` | `0x0000` | `0x00000000` | cluster libero |
| `0xFF7` | `0xFFF7` | `0x0FFFFFF7` | cluster danneggiato |
| `≥ 0xFF8` | `≥ 0xFFF8` | `≥ 0x0FFFFFF8` | **fine del file** |

## 6. FAT12, FAT16, FAT32: cosa cambia

| | FAT12 | FAT16 | FAT32 |
|---|---|---|---|
| Bit per voce | 12 | 16 | 32 (ne valgono **28**: si maschera con `0x0FFFFFFF`) |
| Offset della voce N | N × 3 / 2 | N × 2 | N × 4 |
| Cluster massimi | ~4085 | ~65 525 | ~268 milioni |
| Root directory | regione fissa | regione fissa | **cartella normale** (cluster nel BPB, di solito 2) |
| Uso | floppy | dischi piccoli | dischi fino a 2 TB |

Come si capisce il tipo? **Non** dalla stringa "FAT12" nell'EBR, che è solo informativa:
- se il campo "settori per FAT" a 16 bit vale **0** → FAT32 (il valore vero sta nell'EBR FAT32);
- altrimenti si contano i cluster: **meno di 4085 → FAT12**, altrimenti FAT16.

Il nostro disco da 250 MB in FAT32 (letto dall'immagine): 512 byte/settore, 1 settore/cluster,
**32 settori riservati** (boot, FSInfo nel settore 1, copia di backup del boot nel settore 6), 2 FAT
da **3923 settori**, root al **cluster 2**, 509 952 settori totali. Inizio regione dati =
32 + 2 × 3923 = **7878** (relativo alla partizione).

### I tre bug FAT che ho corretto (e perché erano bug)

1. **FAT32, fine catena**: il codice originale controllava `>= 0xFFFFFFF8`, ma in FAT32 la fine è
   `0x0FFFFFF8`: i 4 bit alti sono riservati e vanno ignorati. Una cartella (che non ha dimensione)
   veniva letta "per sempre". Ora si fa `& 0x0FFFFFFF`.
2. **FAT32, root directory**: veniva letta come settori **consecutivi**, ma in FAT32 la root è una
   catena di cluster che può essere **frammentata**. L'ho verificato con una root di 76 cluster, tutti
   non contigui: ora il kernel si trova lo stesso.
3. **Cache della FAT12**: una voce a 12 bit può stare a cavallo di due settori; il codice ora
   tiene sempre un settore di margine nella cache.

## 7. File dentro le cartelle

Per un percorso come `/folder/demo.txt`:
1. si divide sulle `/`: `folder`, `demo.txt`;
2. si cerca `folder` nella root → cartella, cluster 142;
3. si legge la cartella esattamente come un file (le cartelle sono file pieni di voci da 32 byte) e
   ci si cerca `demo.txt`;
4. e così via fino all'ultimo pezzo.

È ciò che fa `FAT_Open` in `stage2/fat.c`.

## 8. I nomi lunghi (LFN)

Con Windows 95 sono arrivati i nomi lunghi (`long_file_name_number_1.txt`). Sono un "trucco":
prima della voce 8.3 (`LONG_F~1TXT`) ci sono una o più voci finte con attributo **`0x0F`**, che
contengono 13 caratteri UTF-16 ciascuna, in ordine inverso. I vecchi sistemi le ignorano, perché
quella combinazione di attributi non ha senso per un file vero.

Nel progetto il supporto LFN è **iniziato ma commentato** (`FAT_FindFile`): per caricare
`kernel.elf` bastano i nomi 8.3.

## 9. Dove si vede nel codice

- `src/bootloader/stage2/fat.h`, `fat.c`: driver FAT12/16/32 in sola lettura;
- `tools/fat/`: la prima versione del driver (solo FAT12), da compilare ed eseguire su Linux per
  provarlo su un'immagine: `make -C tools/fat` e poi `build/tools/fat.out image.img /test.txt`;
- provare a mano: `mdir -i build/i686_debug/image.img ::` (floppy) oppure
  `MTOOLS_SKIP_CHECK=1 mdir -i build/i686_debug/image.img@@1048576 ::/boot` (disco).
