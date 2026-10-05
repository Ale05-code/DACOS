# Capitolo 2: leggere dal disco

## 1. Il problema: 512 byte non bastano

Il BIOS carica **un solo settore**. In 512 byte non ci sta un sistema operativo, nemmeno un driver
per il file system. La prima cosa che il codice di boot deve fare è quindi **caricare il resto** dal
disco.

Per questo ogni sistema operativo ha un **bootloader**, che in generale:
- carica in memoria i pezzi essenziali del sistema (il **kernel**);
- mette il computer nello stato che il kernel si aspetta (per esempio la modalità a 32 bit);
- raccoglie informazioni che dopo sarebbe difficile ottenere (la **mappa della memoria**, il disco di
  avvio…), perché in modalità a 32 bit i servizi del BIOS non funzionano più.

## 2. Come sono organizzati i dati su un disco

Vale per floppy, hard disk, CD (anche se i dischi moderni "fingono"):

- il disco (*piatto*) è diviso in anelli concentrici: le **tracce**; l'insieme delle tracce alla
  stessa distanza dal centro su tutti i piatti è un **cilindro**;
- ogni traccia è divisa in spicchi: i **settori**, di **512 byte**;
- ogni faccia di ogni piatto ha la sua **testina** (*head*).

### CHS (Cylinder-Head-Sector)

Si indica un settore con tre numeri: cilindro, testina, settore.
- cilindri e testine si contano da **0**;
- i settori si contano da **1** (attenzione!).

Un floppy da 1,44 MB ha 80 cilindri × 2 testine × 18 settori × 512 byte = 1 474 560 byte.

### LBA (Logical Block Addressing)

I settori sono numerati semplicemente 0, 1, 2, 3… È molto più comodo: ci interessa "il settore
numero 19", non dove si trova fisicamente.

### Conversione LBA → CHS

Servono due numeri della geometria: **settori per traccia** (S) e **testine** (H).

```
settore  = (LBA % S) + 1
testina  = (LBA / S) % H
cilindro = (LBA / S) / H
```

**Esempio** (floppy: S = 18, H = 2), LBA 44 (dove inizia la root directory del nostro floppy):
- settore = 44 % 18 + 1 = 8 + 1 = **9**
- testina = (44 / 18) % 2 = 2 % 2 = **0**
- cilindro = (44 / 18) / 2 = 2 / 2 = **1**

→ CHS = (1, 0, 9).

Nel codice: `lba_to_chs` in `stage1/boot.asm` (con `div`: il quoziente va in `AX`, il resto in
`DX`) e `DISK_LBA2CHS` in `stage2/disk.c`.

## 3. Il servizio BIOS: `int 0x13`

### Lettura CHS: `AH = 0x02`

| Registro | Contenuto |
|---|---|
| `AH` | `0x02` |
| `AL` | numero di settori da leggere |
| `CH` | cilindro (8 bit bassi) |
| `CL` | bit 0–5: settore; bit 6–7: bit 8–9 del cilindro |
| `DH` | testina |
| `DL` | numero del drive |
| `ES:BX` | dove mettere i dati in memoria |

In uscita: **Carry Flag = 1 se c'è stato un errore**, `AH` = codice d'errore, `AL` = settori letti.

Il cilindro può arrivare a 10 bit (1023), e i 2 bit alti vanno "infilati" in `CL`. Per questo:

```nasm
mov ch, al          ; 8 bit bassi del cilindro
shl ah, 6           ; i 2 bit alti del cilindro, portati nei bit 6-7
or  cl, ah          ; uniti al numero di settore
```

Il limite del CHS è 1024 cilindri × 255 testine × 63 settori ≈ **8 GB**. Oltre bisogna usare l'LBA.

### Riprovare

I floppy veri sbagliano spesso, quindi la regola è: **riprova 3 volte**, e tra un tentativo e l'altro
fai un **reset del controller** (`AH = 0x00`). Lo fanno sia lo stage1 (`disk_read` +
`disk_reset`) sia lo stage2 (`DISK_ReadSectors`).

Dettaglio importante: prima di `int 13h` il codice esegue `stc` (*set carry*), perché alcuni BIOS
non toccano il flag quando tutto va bene. Così "carry = 1" vuol dire davvero errore solo se il BIOS
l'ha lasciato acceso.

### Leggere un settore alla volta

Molti BIOS (e Bochs) **non** leggono in una sola chiamata settori che stanno su tracce diverse. Su un
floppy una traccia ha 18 settori, quindi chiedere "24 settori a partire dall'1" può fallire. Nel
progetto corretto sia lo stage1 sia lo stage2 leggono **un settore alla volta** quando usano il CHS.

### Le estensioni LBA: `AH = 0x41` e `AH = 0x42`

I BIOS dagli anni '90 in poi hanno le **Enhanced Disk Drive extensions**:

- **`AH = 0x41`, `BX = 0x55AA`**: "ci sono le estensioni?". Se sì, Carry = 0 e `BX = 0xAA55`
  (i byte scambiati).
- **`AH = 0x42`**: lettura LBA. Invece dei registri si passa in `DS:SI` l'indirizzo di una piccola
  struttura, il **DAP** (*Disk Address Packet*):

  ```nasm
  dap:
      .size:      db 10h      ; dimensione della struttura (16 byte)
                  db 0        ; riservato
      .count:     dw 1        ; quanti settori
      .offset:    dw 0x7C00   ; dove: offset...
      .segment:   dw 0        ; ...e segmento
      .lba:       dq 2048     ; LBA del primo settore (64 bit)
  ```

Il nostro codice prova sempre prima le estensioni e, se mancano, usa il CHS:
- MBR (`mbr.asm`): legge il VBR della partizione;
- stage1: legge lo stage2;
- stage2: `x86_Disk_ExtensionsPresent` / `x86_Disk_ExtendedRead` in `x86.asm`, scelti da
  `DISK_ReadSectors` in `disk.c`.

## 4. La geometria del disco: `AH = 0x08`

Per convertire LBA in CHS bisogna conoscere S e H del disco **come li vede il BIOS**. Lo stage2 li
chiede con `int 13h, AH = 0x08` (`x86_Disk_GetDriveParams`):
- `CL` bit 0–5 = settori per traccia;
- `CH` + `CL` bit 6–7 = ultimo cilindro (va aggiunto 1);
- `DH` = ultima testina (va aggiunto 1).

> Lo stage1 invece usa i valori del **BPB** del file system (`bdb_sectors_per_track`,
> `bdb_heads`), scritti da `mkfs.fat`. Vanno bene per il floppy; sul disco si usano comunque le
> estensioni LBA.

## 5. Perché un floppy (all'inizio)

- È il supporto più semplice: niente partizioni, geometria fissa e nota.
- Tutti i BIOS ed emulatori lo supportano.
- L'immagine è un file da 1,44 MB facile da creare.
- Ci sta il file system **FAT12**, il più semplice.

Il progetto attuale supporta sia il floppy (`imageType=floppy`) sia un **hard disk partizionato**
(`imageType=disk`, il default).

## 6. Debug con Bochs

Bochs è utile perché mostra lo stato della CPU anche in real mode. Configurazione (la crea
`scripts/bochs.sh`):

```
megs: 128
romimage: file=/usr/share/bochs/BIOS-bochs-legacy
vgaromimage: file=/usr/share/bochs/VGABIOS-lgpl-latest
mouse: enabled=0
display_library: sdl2, options="gui_debug"
floppya: 1_44="build/i686_debug/image.img", status=inserted
boot: floppy
```

Per il disco: `ata0-master: type=disk, path="...", mode=flat` (la geometria la calcola Bochs).
Nel debugger: `b 0x7c00` (breakpoint), `c` (continua), `s` (un'istruzione), `r` (registri).
