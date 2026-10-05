# Capitolo 14: timer, orologio e tastiera

Per avere una shell interattiva servono tre periferiche che fino alla Parte 11 non erano usate:
il **timer** (per sapere da quanto è acceso il sistema), l'**orologio** (per la data dei file) e
soprattutto la **tastiera**.

## 1. Il timer: PIT 8253/8254 (`arch/i686/pit.c`)

Il **PIT** (*Programmable Interval Timer*) è un contatore che scende a **1 193 182 Hz**. Gli si dà
un numero di partenza (il **divisore**); ogni volta che arriva a zero genera l'**IRQ 0** e
ricomincia.

```
frequenza degli interrupt = 1 193 182 / divisore
divisore per 100 Hz       = 1 193 182 / 100 = 11 931
```

Programmazione:

| Porta | Valore | Significato |
|---|---|---|
| `0x43` (comando) | `0x36` | canale 0, scrivo byte basso poi byte alto, modo 3 (onda quadra) |
| `0x40` (canale 0) | `11931 & 0xFF` | byte basso del divisore |
| `0x40` | `11931 >> 8` | byte alto |

Poi si registra un gestore per l'IRQ 0 e lo si **smaschera** sul PIC (capitolo 10):

```c
static volatile uint64_t g_Ticks = 0;
static void PIT_IRQHandler(Registers* regs) { g_Ticks++; }
...
i686_IRQ_RegisterHandler(0, PIT_IRQHandler);
i686_IRQ_Unmask(0);
```

`g_Ticks / 100` = secondi dall'accensione: è quello che mostrano `uptime` e `neofetch`.

> `volatile` dice al compilatore che la variabile può cambiare "da sola" (dentro un interrupt):
> senza, l'ottimizzatore potrebbe leggerla una volta sola e non accorgersi mai che aumenta.

## 2. L'orologio: RTC nel chip CMOS (`arch/i686/rtc.c`)

Il PC ha un piccolo orologio alimentato da una batteria (l'**RTC**, *Real Time Clock*) che va anche
a computer spento. Sta nel chip **CMOS** e si legge con due porte: si scrive il numero del registro
su `0x70` e si legge il valore da `0x71`.

| Registro | Contenuto |
|---|---|
| `0x00` `0x02` `0x04` | secondi, minuti, ore |
| `0x07` `0x08` `0x09` | giorno, mese, anno (solo 2 cifre) |
| `0x0A` | bit 7 = "sto aggiornando, aspetta" |
| `0x0B` | bit 2 = valori in binario (altrimenti **BCD**), bit 1 = formato 24 ore |

Due trucchi:
- di solito i valori sono in **BCD** (*Binary Coded Decimal*): `0x23` vuol dire 23, non 35.
  Conversione: `(v & 0x0F) + (v >> 4) * 10`;
- l'orologio si aggiorna una volta al secondo: si legge **due volte** finché si ottiene lo stesso
  risultato, così non si prende mai un valore "a metà" (per esempio 23:59 → 00:00).

Con QEMU si usa `-rtc base=localtime` (già in `scripts/run.sh`), così l'orologio segna l'ora italiana
e non quella UTC.

## 3. La tastiera PS/2 (`arch/i686/keyboard.c`)

### Scancode

La tastiera **non manda caratteri**: manda dei numeri che identificano il **tasto fisico**, gli
**scancode**. Il controller (lo stesso 8042 dell'A20!) li mette sulla porta `0x60` e genera l'**IRQ 1**.
Il BIOS imposta lo **scancode set 1**:

- **pressione** → codice (es. `0x1E` = tasto A);
- **rilascio** → codice | `0x80` (es. `0x9E`);
- alcuni tasti aggiunti dopo (frecce, Ctrl destro, AltGr, Canc, Home…) mandano prima il byte
  **`0xE0`**: freccia su = `E0 48`.

| Tasto | Scancode | | Tasto | Scancode |
|---|---|---|---|---|
| Esc | `01` | | Shift sinistro | `2A` |
| 1 … 0 | `02` … `0B` | | Shift destro | `36` |
| Backspace | `0E` | | Ctrl | `1D` (destro: `E0 1D`) |
| Tab | `0F` | | Alt | `38` (AltGr: `E0 38`) |
| Q … P | `10` … `19` | | Spazio | `39` |
| Invio | `1C` | | Caps Lock | `3A` |
| A … L | `1E` … `26` | | F1 … F10 | `3B` … `44` |
| Z … M | `2C` … `32` | | frecce | `E0 48/50/4B/4D` |

### Dallo scancode al carattere

Il driver tiene in memoria quali **modificatori** sono premuti (Shift, Ctrl, Alt, AltGr, Caps Lock)
e usa delle **tabelle** (una per layout) indicizzate dallo scancode:

```c
static const uint8_t g_IT_Normal[] = { [0x10] = 'q', 'w', 'e', ... [0x1A] = 0x8A /* è */, ... };
static const uint8_t g_IT_Shift[]  = { [0x10] = 'Q', 'W', 'E', ... [0x1A] = 0x82 /* é */, ... };
```

La sintassi `[0x10] = 'q', 'w', ...` (*designated initializer*, C99) mette `'q'` nella posizione
`0x10`, `'w'` nella `0x11` e così via.

Regole:
- **Caps Lock** inverte Shift, ma **solo per le lettere**;
- **Ctrl + lettera** dà i codici 1–26 (Ctrl+A = 1, Ctrl+S = 19), come nei terminali Unix: è così che
  l'editor riconosce Ctrl+S e Ctrl+Q;
- i tasti speciali (frecce, F1…) hanno codici ≥ `0x100` (`KEY_UP`, `KEY_DELETE`…).

### Il layout italiano e la code page 437

Lo stesso tasto fisico dà caratteri diversi a seconda del layout: lo scancode `0x1A` è `[` sulla
tastiera americana e `è` su quella italiana. Per questo ci sono due coppie di tabelle (`keymap it`
e `keymap us`).

Il carattere `è` non è ASCII. La scheda VGA usa il font **code page 437** (quello dell'IBM PC), dove
`è` = `0x8A`, `à` = `0x85`, `ò` = `0x95`, `ù` = `0x97`, `ì` = `0x8D`. Il driver produce direttamente
questi valori, quindi a schermo compaiono giusti. Con **AltGr** (`E0 38`) si ottengono `@` (AltGr+ò),
`#` (AltGr+à), `[ ]` (AltGr+è / AltGr++) e `{ }` (Shift+AltGr+è / +).

### Il buffer circolare

L'IRQ arriva quando vuole, la shell legge quando vuole. In mezzo c'è un **buffer circolare** (*ring
buffer*) di 64 tasti:

```
 g_Buffer: [ a ][ b ][ c ][   ][   ] ...
              ▲            ▲
            Tail          Head
 IRQ:   scrive in Head, Head = (Head + 1) % 64
 shell: legge da Tail, Tail = (Tail + 1) % 64        vuoto se Head == Tail
```

`Keyboard_GetKey()` aspetta un tasto **senza consumare la CPU**: esegue `sti; hlt`, che addormenta il
processore fino al prossimo interrupt (della tastiera o del timer).

### Come si vede in QEMU

QEMU manda alla macchina virtuale lo scancode del **tasto fisico** che premi, quindi con una tastiera
italiana e `keymap it` (il default) i tasti corrispondono a quello che c'è scritto sopra. Per far
uscire il mouse dalla finestra di QEMU: **Ctrl+Alt+G**.
