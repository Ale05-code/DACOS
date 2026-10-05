# Capitolo 4: partizioni, MBR e bootloader a due stadi

## 1. Perché due stadi

Per caricare un kernel ELF da un file system FAT servono: un driver del disco, un driver FAT, un
parser ELF, `printf`, il passaggio alla modalità a 32 bit, il rilevamento della memoria. Sono decine
di KB di codice, scritti comodamente in C.

In 512 byte (meno BPB e firma, circa 420 utili) non ci stanno. Si divide quindi il bootloader:

| | Stage1 | Stage2 |
|---|---|---|
| Linguaggio | assembly | C + un po' di assembly |
| Dimensione | 512 byte | ~12 KB (+ ~6 KB di `.bss`) |
| Dove sta sul disco | primo settore della partizione (o del floppy) | settori **fissi**, fuori dal file system |
| Come lo trova chi lo carica | il BIOS o l'MBR | lo stage1 legge una piccola tabella (LBA, numero di settori) scritta dallo script di build |
| Cosa fa | carica lo stage2 a `0x500` e ci salta | tutto il resto, poi salta al kernel |

Il trucco è che lo stage1 **non deve capire il FAT**: legge solo dei settori in posizioni note,
scritte in `stage2_location` (`boot.asm`):

```nasm
global stage2_location
stage2_location:        times 30 db 0       ; (LBA a 4 byte, numero di settori a 1 byte), ..., 0
```

Lo script `image/SConscript` trova l'indirizzo di questa variabile nel file **`stage1.map`**
(prodotto dal linker) e ci scrive `LBA = 1`, `settori = 25`.

## 2. Dove sta lo stage2

- **Floppy**: nei **settori riservati** del FAT12. Si formatta con `mkfs.fat -R 26` (1 boot +
  25 stage2), così il file system "salta" quei settori e lo stage2 sta nei settori 1–25.
- **Disco**: tra l'MBR (settore 0) e l'inizio della partizione (settore 2048) ci sono 2047 settori
  liberi (~1 MB, lasciati vuoti per allineare la partizione). Lo stage2 sta nei settori 1–25. È lo
  stesso trucco che usa GRUB.

## 3. Hard disk: l'MBR e la tabella delle partizioni

Un hard disk di solito è diviso in **partizioni**. Il primo settore del disco è l'**MBR** (*Master
Boot Record*):

```
offset   0 ┌──────────────────────────────┐
           │ codice di boot (440 byte)    │  ← src/bootloader/mbr/mbr.asm
     440   ├──────────────────────────────┤
           │ firma del disco (4) + 0 (2)  │
     446   ├──────────────────────────────┤
           │ partizione 1 (16 byte)       │
           │ partizione 2 (16 byte)       │
           │ partizione 3 (16 byte)       │
           │ partizione 4 (16 byte)       │
     510   ├──────────────────────────────┤
           │ 55 AA                        │
     512   └──────────────────────────────┘
```

Ogni **voce di partizione** (16 byte):

| Offset | Dim. | Campo | Nella nostra immagine |
|---|---|---|---|
| 0 | 1 | stato: `0x80` = **attiva** (avviabile) | `80` |
| 1 | 3 | CHS di inizio | `00 01 10` |
| 4 | 1 | tipo | `0B` = FAT32 |
| 5 | 3 | CHS di fine | `03 E0 FF` |
| 8 | 4 | **LBA di inizio** | `00 08 00 00` = **2048** |
| 12 | 4 | **numero di settori** | `00 C8 07 00` = 509 952 |

(Valori letti dall'immagine compilata, ricordando che sono little-endian.)

### Il chainloading

Il codice dell'MBR:
1. si **sposta** da `0x7C00` a `0x0600`, perché deve lasciare `0x7C00` libero;
2. cerca la partizione **attiva**;
3. ne legge il **primo settore** (il **VBR**, *Volume Boot Record*: per noi lo stage1) a `0x7C00`;
4. controlla la firma `0xAA55` e ci salta, con **`DL` = disco** e **`DS:SI` = puntatore alla voce
   della partizione**.

Questo passaggio di testimone si chiama **chainloading** (caricamento a catena). Lo stage1, per prima
cosa, copia quei 16 byte da `DS:SI` a `0x1000:0000` (= `0x10000`), perché più avanti lo stage2
sovrascriverà la zona dove stava l'MBR.

### Il nostro MBR (e perché l'ho aggiunto)

All'inizio il progetto non aveva un MBR suo: usava quello generico che **libparted** scrive quando
crea la tabella delle partizioni. Quel codice però legge il VBR **solo in CHS**, usando i valori CHS
scritti nella tabella, calcolati con una geometria di 255 testine. Se il BIOS vede il disco con una
geometria diversa (Bochs, per esempio, usa 16 testine) legge il settore sbagliato ed esegue
spazzatura: con Bochs il sistema non partiva.

`src/bootloader/mbr/mbr.asm` prova prima l'**LBA** (`int 13h/42h`, con l'LBA della partizione,
che non dipende dalla geometria) e solo se non c'è usa il CHS. Lo script di build ne scrive **solo i
primi 440 byte**, così la tabella delle partizioni creata da parted resta intatta.

## 4. Lo stage1 passo per passo (`src/bootloader/stage1/boot.asm`)

```
0x7C00  jmp short start ; nop          (3 byte)
0x7C03  BPB + EBR                       (scritti da mkfs.fat: 87 byte in FAT32, 59 in FAT12)
0x7C5A  start:                          (__entry_start, letto dalla mappa del linker)
        1. copia la voce di partizione DS:SI → 1000:0000
        2. DS = ES = SS = 0, SP = 0x7C00
        3. far return → CS:IP = 0000:.after
        4. salva DL
        5. int 13h/41h → ci sono le estensioni LBA?
        6. per ogni voce di stage2_location: legge i settori, uno alla volta, a 0000:0500
        7. DL = disco, SI:DI = voce di partizione, salta a 0000:0500
...     .text: puts, lba_to_chs, disk_read, disk_reset
...     .data/.rodata: messaggi, DAP, stage2_location
0x7DFE  55 AA
```

Lo stage1 è compilato **per ogni file system** (`-DFILESYSTEM=fat32`), perché in FAT32 il BPB è più
lungo e l'etichetta `ebr_drive_number` cambia posizione. Infatti la cartella si chiama
`build/i686_debug/stage1_fat32/`.

## 5. Come si costruisce l'immagine (disk)

`image/SConscript`, funzione `build_disk`:

1. crea un file di 250 MB pieno di zeri (`truncate`: il file è "sparse" e occupa poco spazio
   finché non ci si scrive);
2. **tabella delle partizioni** con pyparted: una partizione primaria attiva, dal settore 2048
   alla fine, tipo FAT32;
3. **MBR**: scrive i 440 byte di `mbr.bin` nel settore 0;
4. **formatta** la partizione: `mkfs.fat -F 32 -n DACOS -R 32 --offset 2048`, poi copia lo stage1 anche nella copia di
   backup del boot sector (settore 6 della partizione), così le due copie restano identiche;
5. **stage1**: scrive nel settore 2048 il salto e il codice, ma non il BPB; poi scrive LBA e
   dimensione dello stage2 in `stage2_location`;
6. **stage2**: lo copia nei settori 1–25;
7. **file**: con mtools crea `/boot`, copia `kernel.elf` e il contenuto di `image/root/`.

Per il **floppy** (`build_floppy`) è uguale ma senza partizioni né MBR, e con lo stage2 nei settori
riservati.

## 6. La mappa finale

```
DISCO (250 MB)
settore 0          MBR (nostro codice + tabella di parted)
settori 1..25      STAGE2
settori 26..2047   vuoti
settore 2048       VBR = BPB FAT32 di mkfs + STAGE1
settore 2049       FSInfo
settore 2054       copia di backup del boot sector (con lo stage1)
settori 2080..     FAT n.1, FAT n.2 (3923 settori ciascuna)
settore 9926       inizio dati = cluster 2 = root directory
                   /boot/kernel.elf, /test.txt, /folder/demo.txt
```

(9926 = 2048 + 7878, vedi capitolo 3.)

## 7. Cose da ricordare

- Il BIOS carica **sempre** a `0x7C00`, con `DL` = disco.
- Chi carica un VBR passa `DS:SI` → voce della partizione.
- Lo stage2 non è un file: è in settori **fissi**, fuori dal file system. Il kernel invece è un file
  normale (`/boot/kernel.elf`) e si può aggiornare copiandolo.
