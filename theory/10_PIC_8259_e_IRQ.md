# Capitolo 10: il PIC 8259 e gli IRQ

## 1. Interrupt hardware: perché servono

Senza interrupt, per sapere se è stato premuto un tasto il kernel dovrebbe chiedere alla tastiera di
continuo (*polling*), sprecando la CPU. Con gli interrupt è il **dispositivo** ad avvisare: "ho un
dato pronto". Questi segnali si chiamano **IRQ** (*Interrupt ReQuest*).

## 2. Il PIC 8259

I dispositivi non sono collegati direttamente alla CPU, ma a un **PIC** (*Programmable Interrupt
Controller*), l'Intel **8259**. Un 8259 ha 8 ingressi; i PC ne hanno **due in cascata**:

```
                       ┌────────────┐
IRQ 0  timer ─────────►│            │
IRQ 1  tastiera ──────►│            │
IRQ 2  ◄── slave ─────►│   MASTER   ├──── INT ───► CPU
IRQ 3  COM2 ──────────►│  porte     │
IRQ 4  COM1 ──────────►│  0x20/0x21 │
IRQ 5  LPT2/audio ────►│            │
IRQ 6  floppy ────────►│            │
IRQ 7  LPT1 ──────────►│            │
                       └─────▲──────┘
                             │ (in cascata sull'IRQ 2)
                       ┌─────┴──────┐
IRQ 8  RTC (orologio) ►│            │
IRQ 9-11 liberi ──────►│   SLAVE    │
IRQ 12 mouse PS/2 ────►│  porte     │
IRQ 13 FPU ───────────►│  0xA0/0xA1 │
IRQ 14 disco primario ►│            │
IRQ 15 disco second. ─►│            │
                       └────────────┘
```

Il PIC:
- riceve i segnali, decide quale ha la **priorità** (il numero più basso vince);
- dice alla CPU **quale vettore** dell'IDT usare (vettore = offset + numero dell'IRQ);
- aspetta l'**EOI** (*End Of Interrupt*) prima di mandare altri interrupt della stessa priorità o
  di priorità più bassa.

## 3. Il problema: i vettori si sovrappongono alle eccezioni

Il BIOS configura il master con **offset 0x08**: IRQ 0 (timer) → vettore 8. Ma il vettore 8 è il
**Double Fault**! Il timer scatta ~18 volte al secondo e sembrerebbe un errore gravissimo.

(Era una scelta di IBM, quando Intel non aveva ancora "prenotato" i vettori 0–31.)

Soluzione: **rimappare** il PIC. Nel progetto: master → `0x20`–`0x27`, slave → `0x28`–`0x2F`,
subito dopo le 32 eccezioni.

## 4. La sequenza di inizializzazione (ICW1–ICW4)

Il PIC si programma scrivendo 4 *Initialization Control Words* sulle sue porte, in quest'ordine
(`i8259_Configure` in `i8259.c`):

| Passo | Porta | Valore | Significato |
|---|---|---|---|
| ICW1 | comando (`0x20`, `0xA0`) | `0x11` | `0x10` = inizializza + `0x01` = arriverà ICW4 |
| ICW2 | dati (`0x21`, `0xA1`) | `0x20` / `0x28` | **offset** dei vettori |
| ICW3 | dati | `0x04` / `0x02` | master: "ho lo slave sull'IRQ **2**" (bit 2 = `0000 0100`); slave: "la mia identità è **2**" |
| ICW4 | dati | `0x01` | modalità 8086 (eventualmente + `0x02` per l'EOI automatico) |

Tra un comando e l'altro c'è `i686_iowait()`: una scrittura sulla porta `0x80`, che non fa nulla ma
richiede un po' di tempo. Il PIC originale era lento e aveva bisogno di questa pausa.

## 5. La maschera (IMR)

Ogni PIC ha un registro di **maschera** di 8 bit (scritto sulla porta dati): bit = 1 → quell'IRQ è
**ignorato**. Il progetto tiene una maschera a 16 bit (master + slave) in `g_PicMask`:

```c
void i8259_Unmask(int irq) { i8259_SetMask(g_PicMask & ~(1 << irq)); }   // abilita
void i8259_Mask(int irq)   { i8259_SetMask(g_PicMask |  (1 << irq)); }   // disabilita
```

Dopo l'inizializzazione **tutto è mascherato**: ogni driver abiliterà il suo IRQ quando sarà pronto.

### Il "Probe": c'è davvero un 8259?

```c
bool i8259_Probe()
{
    i8259_Disable();
    i8259_SetMask(0x1337);              // scrive un valore strano
    return i8259_GetMask() == 0x1337;   // se lo rileggo uguale, il chip c'è
}
```

Sui PC moderni al posto dell'8259 c'è l'**APIC**, che lo emula. Per questo `irq.c` usa un'"interfaccia"
`PICDriver` (una struttura di puntatori a funzione): domani si potrà aggiungere un driver APIC e
scegliere quello che risponde al probe.

## 6. EOI: dire al PIC "ho finito"

Alla fine di ogni IRQ il gestore deve mandare il comando **`0x20`** alla porta comando:
- al **master** sempre;
- anche allo **slave** se l'IRQ era ≥ 8 (perché è passato da tutti e due).

```c
void i8259_SendEndOfInterrupt(int irq)
{
    if (irq >= 8)
        i686_outb(PIC2_COMMAND_PORT, 0x20);
    i686_outb(PIC1_COMMAND_PORT, 0x20);
}
```

Se te lo dimentichi, **non arrivano più interrupt** (il timer si ferma dopo il primo tick).

## 7. Come sono collegati IRQ e ISR (`irq.c`)

```
IRQ 0 del timer
  → il PIC manda il vettore 0x20
  → la CPU esegue i686_ISR32 (dalla IDT)
  → isr_common → i686_ISR_Handler(regs)
  → g_ISRHandlers[0x20] = i686_IRQ_Handler       (registrato da i686_IRQ_Initialize)
  → irq = 0x20 - 0x20 = 0
  → g_IRQHandlers[0] (se qualcuno l'ha registrato), altrimenti "Unhandled IRQ 0"
  → EOI al PIC
  → iret
```

Alla fine `i686_IRQ_Initialize` chiama `i686_EnableInterrupts()` (`sti`): da qui la CPU accetta gli
interrupt.

## 8. Esercizio: far funzionare il timer

In `kernel/main.c` c'è già una funzione:

```c
void timer(Registers* regs)
{
    printf(".");
}
```

Per vederla in azione:
1. togli il commento a `i686_IRQ_RegisterHandler(0, timer);` in `start()`;
2. togli il commento a `g_Driver->Unmask(0);` in fondo a `i686_IRQ_Initialize()` (`irq.c`);
3. `scons run`: lo schermo si riempie di puntini, circa 18 al secondo (il timer PIT, programmato
   dal BIOS a 18,2 Hz).

Con `Unmask(1)` arriva anche l'IRQ della tastiera (per leggere il tasto si legge la porta `0x60`).

## 9. Bug corretti in questa parte

- `i8259_ReadIrqRequestRegister` e `i8259_ReadInServiceRegister` leggevano **due volte lo slave**
  invece di master + slave (non erano ancora usate, ma lo sarebbero state per riconoscere gli
  "spurious IRQ" 7 e 15);
- il messaggio diceva "Found 8259 PIC PIC." (nome ripetuto).
