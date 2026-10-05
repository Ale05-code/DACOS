# Capitolo 16: la shell, l'editor e neofetch

## 1. Cos'è una shell

La **shell** è il programma che legge i comandi e li esegue. Su Linux è un programma come gli altri
(`bash`, `zsh`); in DACOS, che non ha ancora i processi, è una funzione del kernel: `Shell_Run()`
(`shell/shell.c`). Si chiama **dsh** (DACOS shell).

Il ciclo è sempre lo stesso (**REPL**, *Read-Eval-Print Loop*):

```c
for (;;)
{
    Shell_PrintPrompt();        // dacos@DACOS:/docs$
    Shell_ReadLine(line);       // aspetta i tasti fino a Invio
    History_Add(line);
    Shell_Execute(line);        // divide in parole, trova il comando, lo esegue
}
```

## 2. Leggere una riga: il line editor

Non basta stampare i tasti: bisogna poter **correggere**. `Shell_ReadLine` tiene:

- il buffer della riga e la sua lunghezza;
- la posizione del **cursore** dentro la riga;
- la posizione sullo schermo dove inizia la riga (`StartX`, `StartY`).

A ogni tasto modifica il buffer (inserisce o cancella con `memmove`) e **ridisegna** la riga dal
punto di partenza. Se la riga va a capo in fondo allo schermo e lo schermo scorre, anche il punto di
partenza "sale": il codice se ne accorge confrontando dove pensava di finire con dove si trova davvero
il cursore.

| Tasto | Azione |
|---|---|
| ← → | muove il cursore |
| Home / Fine (o Ctrl+A / Ctrl+E) | inizio / fine riga |
| Backspace / Canc | cancella prima / sotto il cursore |
| ↑ ↓ | **cronologia** (gli ultimi 32 comandi) |
| Tab | **completamento** di comandi e nomi di file |
| Ctrl+C | annulla la riga |
| Ctrl+L | pulisce lo schermo |
| Ctrl+U | cancella tutto prima del cursore |

### Il completamento con Tab

1. si prende la parola sotto il cursore (`cat fo`);
2. se è la prima parola, si cercano i **comandi** che iniziano così; altrimenti si divide in
   "cartella" + "inizio del nome" e si elencano i file di quella cartella;
3. con **una** sola corrispondenza si completa (aggiungendo `/` se è una cartella);
4. con **più** corrispondenze si completa la parte comune; se non c'è, le si mostra tutte.

## 3. Dalla riga ai comandi: il parsing

`Shell_Parse` divide la riga in **parole** (`argv`), come fa `main(int argc, char** argv)` in C:

```
echo "ciao mondo" > saluti.txt
 argv[0] = "echo"    argv[1] = "ciao mondo"    redirezione: > saluti.txt
```

- gli spazi separano le parole, le **virgolette** (`"…"` o `'…'`) le tengono unite;
- `>` e `>>` indicano una **redirezione**.

Poi si cerca `argv[0]` nella **tabella dei comandi** (`shell/commands.c`):

```c
const Command g_Commands[] = {
    { "ls",   Cmd_Ls,   "ls [-l] [-a] [percorso...]", "elenca file e cartelle" },
    { "cat",  Cmd_Cat,  "cat file...",                "mostra il contenuto di un file" },
    ...
};
```

Ogni comando è una funzione `int Cmd_X(int argc, char** argv)`, che restituisce 0 se va tutto bene.

### Aggiungere un comando (esercizio)

1. scrivi la funzione in `commands.c`:

   ```c
   static int Cmd_Ciao(int argc, char** argv)
   {
       printf("Ciao %s!\n", argc > 1 ? argv[1] : "mondo");
       return 0;
   }
   ```

2. aggiungila alla tabella: `{ "ciao", Cmd_Ciao, "ciao [nome]", "saluta" },`
3. (facoltativo) aggiungila alla lista per categorie in `Cmd_Help`;
4. `scons run`, poi scrivi `ciao DACOS`.

## 4. La redirezione `>` e `>>`

Come si fa a mettere in un file l'output di **qualsiasi** comando (`ls > lista.txt`) senza modificare
ogni comando? Con la **cattura** nel VFS (`hal/vfs.c`, capitolo 7):

```c
VFS_StartCapture(buffer, 64 KB);     // da ora stdout finisce nel buffer, non sullo schermo
command->Function(argc, argv);       // il comando usa printf come sempre
length = VFS_StopCapture();
FAT_WriteFile(path, buffer, length); // >  : sovrascrive
                                     // >> : prima legge il file, aggiunge in fondo, riscrive
```

È lo stesso principio di Unix: i programmi scrivono su "stdout" senza sapere dove finisce.
I messaggi di errore vanno su **stderr**, che non viene catturato, quindi compaiono comunque a
schermo.

## 5. I percorsi

L'utente scrive percorsi relativi (`../docs/nota.txt`), ma il driver FAT vuole percorsi assoluti
puliti. `Path_Resolve(cwd, input)` (`fs/path.c`):

1. se `input` non inizia con `/`, gli mette davanti la cartella corrente: `/progetti` + `/` +
   `../docs/nota.txt`;
2. lo divide sulle `/`;
3. salta i `.`, e per ogni `..` toglie l'ultimo pezzo;
4. ricompone: `/docs/nota.txt`.

La **cartella corrente** è solo una stringa (`g_Cwd`): `cd` controlla che il percorso esista e sia
una cartella, poi la aggiorna.

## 6. L'editor (`shell/editor.c`)

Un editor a schermo intero, ispirato a **nano**:

```
riga 0-22   testo                       ~ = oltre la fine del file
riga 23     barra di stato (invertita): nome del file, [modificato], riga e colonna
riga 24     tasti (^S Salva  ^Q Esci ...) oppure un messaggio
```

- Tutto il file sta in un buffer (`g_FileBuffer`, 64 KB) come testo con `\n`. Il **cursore** è
  un indice nel buffer.
- Inserire = `memmove` del resto del testo di una posizione in avanti, poi scrivere il carattere;
  cancellare = `memmove` all'indietro.
- Riga e colonna si calcolano contando i `\n`: con file da pochi KB è istantaneo.
- Muovendosi con ↑ ↓ l'editor ricorda la **colonna desiderata** (`WantedColumn`), così passando da una
  riga corta il cursore torna dov'era.
- Lo schermo viene **ridisegnato tutto** a ogni tasto, scrivendo direttamente nella memoria video
  (`VGA_PutCharAt`): sono 2000 celle, la CPU non se ne accorge nemmeno.
- **Ctrl+S** salva con `FAT_WriteFile`. **Ctrl+Q** esce, ma se ci sono modifiche non salvate chiede
  conferma (bisogna premerlo due volte).

## 7. neofetch (`shell/neofetch.c`)

Il disegno della papera viene da `theory/ascii_art.txt`, convertito in C da
`build_scripts/generate_ascii_art.py`:

- i caratteri `░ ▒ ▓ █` diventano `0xB0 0xB1 0xB2 0xDB` (code page 437);
- tolti i margini vuoti, il disegno è di 25 righe × 54 colonne. Lo schermo ha 25 righe e ne serve una
  per il prompt, quindi lo script toglie **una di due righe identiche** del corpo → 24 righe;
- perché 54 colonne? Ogni "pixel" del disegno è fatto di **2 caratteri**: un carattere VGA è alto il
  doppio di quanto è largo (9×16 pixel), e così le proporzioni sono giuste.

Ogni riga = disegno colorato (giallo per `░`, marrone per becco e zampe, grigio per l'occhio) +
spazi fino alla colonna 56 + una riga di informazioni (sistema, uptime, CPU con l'istruzione
`cpuid`, memoria dalla mappa E820, spazio sul disco) + la barra dei 16 colori.

Per cambiare disegno:

```bash
python3 build_scripts/generate_ascii_art.py theory/ascii_art.txt src/kernel/shell/ascii_art.h
scons run
```

## 8. Riavvio e spegnimento (`arch/i686/power.c`)

- **reboot**: si chiede al controller della tastiera (8042) di attivare la linea di reset della CPU
  (comando `0xFE` sulla porta `0x64`). Se non funziona, si carica una IDT vuota e si provoca un
  interrupt: senza gestore → triple fault → la CPU si resetta comunque.
- **shutdown**: un sistema vero legge le tabelle **ACPI** per sapere come spegnere. Gli emulatori hanno
  porte fisse: `outw(0x604, 0x2000)` per QEMU, `outw(0xB004, 0x2000)` per Bochs, `outw(0x4004, 0x3400)`
  per VirtualBox. Se nessuna funziona, DACOS dice che si può spegnere a mano e ferma la CPU.
