# Capitolo 12: il sistema di build (SCons)

## 1. Da make a SCons

Fino alla Parte 10 si usava **make**. Con stage1, stage2, MBR, kernel, una libreria, immagini floppy
e disco, file system diversi e due configurazioni (debug/release), i Makefile diventano illeggibili.
**SCons** usa Python, quindi la logica (per esempio "crea la partizione, formatta, copia i file")
si scrive con funzioni normali.

Concetti di base:
- **`SConstruct`**: il file principale (come il Makefile principale);
- **`SConscript`**: i file delle sottocartelle, richiamati dal principale;
- **Environment**: un insieme di strumenti e opzioni (compilatore, flag, percorsi);
- **Builder**: `env.Object(...)`, `env.Program(...)`, `env.StaticLibrary(...)`,
  `env.Command(...)`: dicono "da questi file produci quest'altro";
- **dipendenze automatiche**: SCons legge gli `#include` da solo, e ricompila un `.c` se cambia uno
  degli header che include;
- **variant dir**: i file compilati vanno in `build/i686_debug/…`, non in mezzo ai sorgenti.

## 2. `SConstruct` passo per passo

### Le variabili di configurazione

```python
VARS = Variables('build_scripts/config.py', ARGUMENTS)
VARS.AddVariables(
    EnumVariable("config",      default="debug",  allowed_values=("debug", "release")),
    EnumVariable("arch",        default="i686",   allowed_values=("i686")),
    EnumVariable("imageType",   default="disk",   allowed_values=("floppy", "disk")),
    EnumVariable("imageFS",     default="fat32",  allowed_values=("fat12", "fat16", "fat32")),
    EnumVariable("mountMethod", default="mtools", allowed_values=("mtools", "guestfs", "mount")),
)
VARS.Add("imageSize", default="250m", converter=ParseSize)
VARS.Add("toolchain", default="toolchain")
```

L'ordine di precedenza è: **riga di comando** (`scons imageFS=fat16`) → `config.py` → default.
`scons -h` mostra l'elenco.

### Due ambienti

- **HOST_ENVIRONMENT**: gli strumenti della tua macchina (serve per i comandi `run`, `debug`…).
  Flag comuni: `-std=c99`, `-std=c++17`, `-g` (simboli di debug), `-O0` in debug, `-O3` in release.
- **TARGET_ENVIRONMENT**: lo stesso, ma con il **cross-compiler**:

```python
TARGET_ENVIRONMENT = HOST_ENVIRONMENT.Clone(
    AR = 'i686-elf-ar', CC = 'i686-elf-gcc', CXX = 'i686-elf-g++', LD = 'i686-elf-g++', ...)
TARGET_ENVIRONMENT.Append(
    ASFLAGS   = ['-f', 'elf', '-g'],
    CCFLAGS   = ['-ffreestanding', '-nostdlib'],
    CXXFLAGS  = ['-fno-exceptions', '-fno-rtti'],    # niente eccezioni e RTTI in C++: servirebbe un runtime
    LINKFLAGS = ['-nostdlib'],
    LIBS      = ['gcc'],                              # libgcc (capitolo 6)
)
```

### L'ordine dei moduli

```python
SConscript('src/libs/core/SConscript',          variant_dir=variantDir + '/libs/core')
SConscript('src/bootloader/mbr/SConscript',     variant_dir=variantDir + '/mbr')
SConscript('src/bootloader/stage1/SConscript',  variant_dir=variantDir + '/stage1_fat32')
SConscript('src/bootloader/stage2/SConscript',  variant_dir=variantDir + '/stage2')
SConscript('src/kernel/SConscript',             variant_dir=variantDir + '/kernel')
SConscript('image/SConscript',                  variant_dir=variantDir)
```

Ogni `SConscript` "esporta" il suo risultato (`Export('stage2')`) e il successivo lo "importa"
(`Import('stage2')`). Alla fine `Default(image)`: con `scons` senza argomenti si costruisce
l'immagine.

### I target "phony"

```python
PhonyTargets(HOST_ENVIRONMENT,
             run=['./scripts/run.sh', imageType, image],
             debug=['./scripts/debug.sh', imageType, image],
             bochs=['./scripts/bochs.sh', imageType, image],
             toolchain=['./scripts/setup_toolchain.sh', toolchain])
Depends('run', image)
```

`scons run` prima ricostruisce l'immagine se serve, poi lancia lo script.

## 3. Un `SConscript` tipico: lo stage2

```python
env = TARGET_ENVIRONMENT.Clone()
env.Append(
    LINKFLAGS = ['-Wl,-T', 'linker.ld', '-Wl,-Map=stage2.map'],   # linker script e mappa
    CPPPATH   = [ '.', 'src/libs' ],                              # dove cercare gli #include
    ASFLAGS   = [ '-I', '.' ],                                    # dove cercare gli %include
)
sources = GlobRecursive(env, '*.c') + GlobRecursive(env, '*.cpp') + GlobRecursive(env, '*.asm')
objects = env.Object(sources)

# crti.o e crtn.o vanno in testa e in coda (capitolo 6)
obj_crti = objects.pop(FindIndex(objects, lambda o: IsFileName(o, 'crti.o')))
obj_crtn = objects.pop(FindIndex(objects, lambda o: IsFileName(o, 'crtn.o')))
objects = [obj_crti, 'crtbegin.o', *objects, libcore, 'crtend.o', obj_crtn]

stage2 = env.Program('stage2.bin', objects)
Export('stage2')
```

- `GlobRecursive` (`build_scripts/utility.py`) trova i file in tutte le sottocartelle: **per
  aggiungere un file `.c` basta crearlo**, non serve toccare la build.
- L'MBR invece non si linka: `env.Command('mbr.bin', 'mbr.asm', 'nasm -f bin -o $TARGET $SOURCE')`.

## 4. La creazione dell'immagine

È spiegata nel capitolo 4 (`image/SConscript`). Riassunto dei comandi esterni usati:

```bash
mkfs.fat image.img -F 32 -n DACOS -R 32 --offset 2048    # formatta la partizione
mmd   -i image.img@@1048576 ::boot                        # crea /boot (mtools)
mcopy -i image.img@@1048576 kernel.elf ::boot/            # copia il kernel
```

Il file system si può popolare in tre modi (`mountMethod`):

| Metodo | Come | sudo? | Note |
|---|---|---|---|
| **mtools** (default) | scrive direttamente nell'immagine | no | funziona ovunque; solo FAT |
| guestfs | `guestmount` (una mini macchina virtuale) | no | su Ubuntu fallisce: serve `/boot/vmlinuz` leggibile |
| mount | `losetup` + `mount` | sì | chiede la password |


## 5. Cosa viene prodotto

```
build/i686_debug/
├── mbr/mbr.bin                    440 byte
├── stage1_fat32/stage1.bin        512 byte   + stage1.map
├── stage2/stage2.bin              ~12 KB     + stage2.map
├── kernel/kernel.elf              ~70 KB (con i simboli di debug) + kernel.map
├── kernel/kernel-stripped.elf     senza simboli
├── libs/core/libcore.o            libreria statica C++
└── image.img                      l'immagine da avviare
```

## 6. Trucchi utili

- `scons -c`: cancella tutto (equivale a `make clean`);
- `scons -Q`: output più pulito;
- `scons build/i686_debug/kernel/kernel.elf`: costruisce solo il kernel;
- se cambi **solo** `image/SConscript` o `config.py`, SCons potrebbe non accorgersi che l'immagine
  va rifatta: cancellala (`rm build/i686_debug/image.img`) o fai `scons -c`;
- **mai `sudo scons`**: i file in `build/` diventerebbero di root.
