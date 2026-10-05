# Capitolo 7: scrivere a schermo (VGA, porta E9 e printf)

## 1. Senza BIOS, come si stampa?

In real mode usavamo `int 0x10`. In protected mode il BIOS non c'è più, quindi bisogna parlare
**direttamente con la scheda video**. Per fortuna in modalità testo è facilissimo.

## 2. La memoria video in modalità testo

All'avvio la scheda video è in **modalità testo 80×25** (80 colonne, 25 righe). Quello che vedi a
schermo è il contenuto di una zona di memoria che parte da **`0xB8000`**. Ogni carattere occupa
**2 byte**:

```
byte 0: codice ASCII del carattere
byte 1: attributo = colore di sfondo (4 bit alti) + colore del testo (4 bit bassi)
```

La cella in colonna `x` e riga `y` sta a:

```
0xB8000 + 2 × (y × 80 + x)
```

Esempio: la "N" di "Nanobyte OS" in alto a sinistra è a `0xB8000`; il primo carattere della seconda
riga è a `0xB8000 + 2 × 80 = 0xB80A0`.

Colori (4 bit): 0 nero, 1 blu, 2 verde, 3 ciano, 4 rosso, 5 magenta, 6 marrone, **7 grigio chiaro**,
8 grigio scuro, … 15 bianco. Il default del progetto è `0x07` = grigio su nero.

```c
void VGA_putchr(int x, int y, char c)
{
    g_ScreenBuffer[2 * (y * SCREEN_WIDTH + x)] = c;
}
```

### Andare a capo, tabulazioni, scorrimento

Lo schermo è solo una matrice: le "righe" le gestiamo noi in `VGA_putc`:
- `\n` → x = 0, y++;
- `\r` → x = 0;
- `\t` → spazi fino al prossimo multiplo di 4;
- se x arriva a 80 → nuova riga;
- se y arriva a 25 → **scrollback**: ogni riga viene copiata su quella sopra e l'ultima viene
  svuotata.


## 3. Il cursore e le porte di I/O

Il cursore lampeggiante non si sposta da solo: va detto al **controller CRT** della VGA. Qui
entrano in gioco le **porte di I/O**, un secondo spazio di indirizzi (0–65535), separato dalla
memoria, con cui la CPU parla con l'hardware tramite le istruzioni `in` e `out`:

```nasm
out dx, al      ; scrive il byte AL sulla porta DX
in  al, dx      ; legge un byte dalla porta DX
```

Il controller CRT ha un registro **indice** (porta `0x3D4`) e un registro **dati** (`0x3D5`): si
scrive prima quale registro interno si vuole, poi il valore. La posizione del cursore
(`y × 80 + x`) è nei registri `0x0F` (byte basso) e `0x0E` (byte alto):

```c
i686_outb(0x3D4, 0x0F);  i686_outb(0x3D5, pos & 0xFF);
i686_outb(0x3D4, 0x0E);  i686_outb(0x3D5, (pos >> 8) & 0xFF);
```

`i686_outb` / `i686_inb` sono due funzioncine assembly (`io_asm.asm`), perché il C non ha un modo
standard di fare `in`/`out`.

## 4. La porta 0xE9: un canale di log gratis

QEMU e Bochs hanno un trucco chiamato **"E9 hack"**: ogni byte scritto sulla porta `0xE9` viene
inoltrato fuori dalla macchina virtuale. Con `qemu -debugcon stdio` compare nel **terminale** da cui
hai lanciato QEMU.

È comodissimo perché:
- lo schermo VGA ha solo 25 righe; il terminale tiene tutto;
- puoi copiare il log, cercarci dentro, salvarlo;
- funziona anche quando lo schermo è rovinato o il sistema è in crash.

Lo stage2 manda ogni carattere sia a schermo sia su E9; il kernel manda i messaggi di log solo su E9.

### Colori ANSI

Il terminale di Linux interpreta le **sequenze di escape ANSI**: `ESC [ codici m`. Il kernel le usa
per colorare i livelli di log (`debug.c`):

| Livello | Sequenza | Aspetto |
|---|---|---|
| debug | `\033[2;37m` | grigio tenue |
| info | `\033[37m` | grigio |
| warning | `\033[1;33m` | giallo grassetto |
| error | `\033[1;31m` | rosso grassetto |
| critical | `\033[1;37;41m` | bianco su sfondo rosso |
| reset | `\033[0m` | normale |

`\033` è il carattere ESC (27 in decimale).

## 5. Un VFS embrionale (`kernel/hal/vfs.c`)

Il kernel non chiama direttamente VGA o E9: scrive su un **file descriptor**, come in Unix:

| fd | Nome | Va a |
|---|---|---|
| 0 | `VFS_FD_STDIN` | (ignorato) |
| 1 | `VFS_FD_STDOUT` | schermo VGA |
| 2 | `VFS_FD_STDERR` | schermo VGA |
| 3 | `VFS_FD_DEBUG` | porta E9 |

```c
printf(...)          == fprintf(VFS_FD_STDOUT, ...)
log_info("Main", …)  → fprintf(VFS_FD_DEBUG, ...)
```

Domani si potranno aggiungere file veri, una porta seriale, ecc., senza toccare `printf`.

## 6. Come funziona `printf`

`printf` è una **macchina a stati** che legge il formato carattere per carattere:

```
NORMAL ──'%'──► LENGTH ──'h'──► LENGTH_SHORT ──'h'──► SPEC
                   │                                  ▲
                   ├──'l'──► LENGTH_LONG ──'l'────────┤
                   └──altro────────────────────────────┘ (va direttamente a SPEC)
SPEC: 'c' 's' 'd' 'i' 'u' 'x' 'X' 'p' 'o' '%'  → stampa e torna a NORMAL
```

| Formato | Tipo letto | Base |
|---|---|---|
| `%c` | `int` (convertito in char) | |
| `%s` | `const char*` | |
| `%d` `%i` | `int` con segno | 10 |
| `%u` | `unsigned` | 10 |
| `%x` `%X` `%p` | `unsigned` | 16 |
| `%o` | `unsigned` | 8 |
| `%ld`, `%lld`, `%llx`… | `long`, `long long` | |

### Argomenti variabili (`stdarg.h`)

```c
void printf(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);            // args punta al primo argomento dopo fmt
    ...
    int n = va_arg(args, int);      // legge il prossimo argomento come int
    ...
    va_end(args);
}
```

Per questo il tipo nel formato **deve** corrispondere all'argomento: con `%x` per un `uint64_t` si
leggerebbero solo 4 degli 8 byte e tutti gli argomenti successivi slitterebbero. Infatti per gli
indirizzi a 64 bit della mappa della memoria si usa `%llx`.

### Convertire un numero in testo

```c
void printf_unsigned(unsigned long long number, int radix)
{
    char buffer[32];
    int pos = 0;
    do {
        buffer[pos++] = "0123456789abcdef"[number % radix];   // ultima cifra
        number /= radix;
    } while (number > 0);
    while (--pos >= 0)          // le cifre sono uscite al contrario
        putc(buffer[pos]);
}
```

Esempio: 1234 in base 16 → `1234 % 16 = 2` ('2'), `77 % 16 = 13` ('d'), `4 % 16 = 4` ('4') →
buffer "2d4" → stampato al contrario: **"4d2"**.
