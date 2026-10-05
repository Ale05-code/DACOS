# Capitolo 15: il disco ATA e il file system FAT in scrittura

Il bootloader legge il disco con il **BIOS** (`int 0x13`), che però funziona solo in real mode. Il
kernel è in protected mode: per creare e salvare file deve parlare **direttamente** con il disco e
saper **scrivere** il FAT, non solo leggerlo.

## 1. Il controller ATA (IDE) in modalità PIO (`arch/i686/ata.c`)

I dischi IDE/ATA (e quelli che QEMU e Bochs emulano con `-hda` / `ata0-master`) si comandano con
otto porte di I/O del **canale primario**:

| Porta | Lettura | Scrittura |
|---|---|---|
| `0x1F0` | dati (16 bit) | dati (16 bit) |
| `0x1F1` | errore | |
| `0x1F2` | | numero di settori |
| `0x1F3` `0x1F4` `0x1F5` | | LBA bit 0-7, 8-15, 16-23 |
| `0x1F6` | | `0xE0` \| LBA bit 24-27 (bit 6 = modalità LBA, bit 4 = master) |
| `0x1F7` | **stato** | **comando** |

Bit del registro di stato: **BSY** (`0x80`) occupato, **DRQ** (`0x08`) dati pronti, **ERR** (`0x01`),
**DF** (`0x20`) guasto.

### Leggere un settore (comando `0x20`)

```
1. aspetta che BSY = 0
2. 0x1F6 ← 0xE0 | (lba >> 24)      0x1F2 ← n settori
   0x1F3 ← lba      0x1F4 ← lba >> 8      0x1F5 ← lba >> 16
3. 0x1F7 ← 0x20 (READ SECTORS)
4. per ogni settore: aspetta BSY = 0 e DRQ = 1, poi leggi 256 word da 0x1F0 (rep insw)
```

Scrivere (`0x30`) è uguale, ma al punto 4 si **mandano** 256 word (`outsw`). Alla fine si dà il
comando `0xE7` (**FLUSH CACHE**), perché il disco può tenere i dati in una sua memoria interna prima
di scriverli davvero.

**PIO** (*Programmed I/O*) vuol dire che è la CPU a spostare ogni word. È lento rispetto al DMA, ma
semplicissimo. Il driver aspetta interrogando lo stato (**polling**) invece di usare l'IRQ 14.

### IDENTIFY (comando `0xEC`)

All'avvio si chiede al disco di descriversi: risponde con 256 word. La word 60-61 contiene il numero
di settori, le word 27-46 il nome del modello (due caratteri per word, scambiati). Se il canale non
c'è, la porta di stato vale `0xFF` ("bus flottante"); se c'è un CD-ROM (ATAPI), i registri LBA
contengono una firma diversa da zero.

## 2. Trovare la partizione

Il kernel non sa dove inizia il file system: legge il **settore 0** del disco. Se è un MBR cerca nella
tabella delle partizioni (capitolo 4) una partizione di tipo FAT (`0x01 0x04 0x06 0x0B 0x0C 0x0E`).
Se invece il settore 0 è già un boot sector FAT (un disco senza partizioni), usa quello. Da lì in poi
tutti gli LBA sono **relativi all'inizio della partizione**.

## 3. La cache dei settori (write-through)

Il driver passa ogni lettura e scrittura da una piccola **cache** di 32 settori:

```c
CacheEntry* entry = &g_Cache[lba % 32];      // "direct mapped": ogni LBA ha un posto fisso
if (entry->Valid && entry->Lba == lba)
    return entry->Data;                       // già in memoria
ATA_ReadSectors(...);                         // altrimenti lo legge dal disco
```

Le scritture sono **write-through**: aggiornano la cache **e** il disco subito. Si perde un po' di
velocità, ma il disco è sempre aggiornato: se spegni la macchina virtuale di colpo, non perdi niente.

## 4. Leggere e scrivere la FAT

`Fat_Get(cluster)` e `Fat_Set(cluster, valore)` lavorano sulle tre varianti:

| | Offset della voce | Lettura | Scrittura |
|---|---|---|---|
| FAT12 | `c + c/2` | 2 byte; dispari `>> 4`, pari `& 0xFFF` | si modifica **solo il mezzo byte** giusto |
| FAT16 | `c × 2` | 2 byte | 2 byte |
| FAT32 | `c × 4` | 4 byte `& 0x0FFFFFFF` | si **conservano i 4 bit alti** |

`Fat_Set` scrive in **tutte le copie** della FAT (di solito 2): devono restare identiche.

### Allocare e liberare cluster

```
Cluster_Allocate(precedente):
    cerca un cluster con valore 0 (libero), partendo da g_NextFree
    FAT[nuovo]       = fine catena (0x0FFFFFFF)
    FAT[precedente]  = nuovo                  ← lo aggancia alla catena
Chain_Free(primo):
    segue la catena e mette 0 in ogni voce
```

Il driver tiene anche il **numero di cluster liberi** (contato all'avvio) e lo scrive nel settore
**FSInfo** di FAT32. Così Linux e `fsck` trovano il valore giusto.

## 5. Le cartelle come sequenze di "slot"

Una cartella è una sequenza di voci da 32 byte (**slot**): nella root di FAT12/16 sono in una zona
fissa, altrove in una catena di cluster. Il codice nasconde la differenza con un **iteratore**
(`DirIter`): "dammi la posizione (settore, offset) dello slot numero N". Se una cartella è piena, si
**allunga** con un nuovo cluster azzerato (`Dir_Extend`).

Valore del primo byte di uno slot:

| Primo byte | Significato |
|---|---|
| `0x00` | libero, e anche tutti quelli dopo |
| `0xE5` | cancellato (libero) |
| altro | in uso |

**Cancellare un file** = scrivere `0xE5` nel primo byte delle sue voci + liberare la sua catena. I
dati restano sul disco finché qualcuno non riusa quei cluster: è per questo che i programmi di
"recupero file cancellati" funzionano.

## 6. Nomi lunghi (LFN) in scrittura

Un nome come `Lista della Spesa.txt` non sta in 8.3. Si fa come Windows:

1. si crea un **alias 8.3** unico: maiuscolo, senza spazi, 6 caratteri + `~1` →
   `LISTAD~1.TXT` (se esiste già, `~2`, `~3`…);
2. si divide il nome in pezzi da **13 caratteri UTF-16**: qui sono 21 caratteri → 2 pezzi;
3. si calcola il **checksum** dell'alias:

   ```c
   for (int i = 0; i < 11; i++)
       sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
   ```

4. si cercano **3 slot liberi consecutivi** e si scrivono **in ordine inverso**:

   ```
   slot n     LFN  ordine 2 | 0x40 (= ultimo pezzo)  "Spesa.txt" + 0x0000 + 0xFFFF...
   slot n+1   LFN  ordine 1                          "Lista della "
   slot n+2   8.3  LISTAD~1.TXT  (attributi, cluster, dimensione, date)
   ```

Ogni voce LFN ha attributo `0x0F` (combinazione impossibile per un file normale: i vecchi sistemi la
ignorano) e il checksum dell'alias. In lettura il nome lungo vale solo se i pezzi sono tutti presenti,
in ordine, e il checksum corrisponde.

### Il trucco dei nomi minuscoli

Un nome come `nota.txt` sta in 8.3, ma in 8.3 si scrive solo in maiuscolo (`NOTA    TXT`). Windows NT
e Linux usano il byte 12 della voce: bit `0x08` = "nome in minuscolo", `0x10` = "estensione in
minuscolo". DACOS li usa, così `nota.txt` non ha bisogno di un nome lungo e Linux lo mostra in
minuscolo.

## 7. Scrivere un file senza rischi

`FAT_WriteFile` segue un ordine preciso:

1. alloca una **nuova** catena e ci scrive i dati;
2. aggiorna la voce della cartella (primo cluster, dimensione, data);
3. solo alla fine libera la **vecchia** catena.

Se qualcosa va storto a metà (disco pieno, errore), il vecchio contenuto è ancora integro.

## 8. Spostare e rinominare

`mv` non copia i dati: crea una **nuova voce** (nella cartella di destinazione) che punta agli
**stessi cluster** e cancella la vecchia. Se si sposta una **cartella**, bisogna anche aggiornare la
sua voce `..`, perché ora il "padre" è un altro.

## 9. Come ho verificato che il file system resti corretto

Dopo ogni test l'immagine è stata controllata da Linux:

```bash
dd if=build/i686_debug/image.img of=part.img bs=512 skip=2048    # estrae la partizione
fsck.fat -n part.img                                             # controlla tutto, senza modificare
MTOOLS_SKIP_CHECK=1 mdir -i build/i686_debug/image.img@@1048576 ::/
```

`fsck.fat` controlla catene, cluster persi, voci doppie, nomi lunghi, il conteggio dei cluster
liberi e la copia di backup del boot sector: nessun errore, anche con 40 file a nome lungo in una
cartella su più cluster.
