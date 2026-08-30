# DACOS

A tiny x86 operating system built from scratch in real-mode 16-bit assembly, started from the [x86 OS Development playlist](https://www.youtube.com/playlist?list=PLFjM7v6KGMpiH2G-kT781ByCNC_0pKpPN) on YouTube and then personalized and extended beyond it.

This is a learning project: the goal is to understand, at the lowest possible level, how a PC boots and how an operating system takes control of the hardware - starting from a BIOS boot sector and building up from there. The playlist is used as a foundation, not a script to copy verbatim - features, structure, and code are being adapted and extended with original work as the project grows.

## What it does so far

- **Boot sector (`src/bootloader/boot.asm`)** - a 512-byte x86 boot sector with a valid FAT12 BIOS Parameter Block (BPB), assembled to run at the classic real-mode load address `0x7C00`.
- **Disk read via BIOS interrupts** - the bootloader calls `INT 13h` (function `02h`) to read raw sectors from the boot floppy, with:
  - LBA → CHS address translation (`lba_to_chs`)
  - a retry loop (up to 3 attempts) with disk controller reset on failure (`disk_reset`)
  - a boot-halt error path if all retries fail
- **FAT12 floppy image** - the build produces a 1.44 MB floppy image (`main_floppy.img`), formatted as FAT12, containing the boot sector and a separate `kernel.bin` file copied onto the filesystem.
- **Minimal kernel stub (`src/kernel/main.asm`)** - currently a placeholder; it is written to the disk image but not yet loaded/executed by the bootloader (that's the next step in the series).

Everything runs in 16-bit real mode - no protected mode, no paging, no C code yet. Just the CPU, the BIOS, and raw assembly.

## How it's implemented

| Piece | Details |
|---|---|
| Architecture | x86 (16-bit real mode) |
| Assembler | [NASM](https://www.nasm.us/) |
| Disk format | FAT12, 3.5" 1.44 MB floppy image |
| Build system | GNU Make |
| Emulation | QEMU (`qemu-system-i386`) |
| Debugging | Bochs (with the Bochs Enhanced Debugger GUI) and QEMU + GDB remote stub |

The boot sector is assembled directly to a flat binary with `org 0x7C00`, since that's the physical memory address the BIOS loads and jumps to after POST. The disk image is built by writing the compiled boot sector to sector 0 with `dd`, formatting the image as FAT12 with `mkfs.fat`, and copying the kernel binary onto the resulting filesystem with `mcopy` (from `mtools`) - without ever mounting the image on the host.

## Project structure

```
DACOS/
├── src/
│   ├── bootloader/
│   │   └── boot.asm        # boot sector: BPB, disk I/O, LBA/CHS conversion
│   └── kernel/
│       └── main.asm        # kernel stub (not yet loaded by the bootloader)
├── build/                  # generated: bootloader.bin, kernel.bin, main_floppy.img
├── theory/                 # personal notes taken while following the series
├── Makefile
├── run.sh                  # boots the built image in QEMU
├── debug.sh                # boots the built image in Bochs (GUI debugger)
├── bochs_config            # Bochs machine configuration
└── bx_enh_dbg.ini          # Bochs Enhanced Debugger UI settings
```

## Prerequisites

Tested on Ubuntu/Debian. You'll need:

- `make`
- `nasm` - the assembler
- `qemu-system-x86` - to run the OS in emulation
- `dosfstools` - provides `mkfs.fat`, used to format the floppy image
- `mtools` - provides `mcopy`, used to copy files onto the FAT12 image without mounting it
- `bochs` (optional) - an alternative emulator with a much better low-level debugger, used for step-by-step CPU/register inspection

Install everything with:

```bash
sudo apt update
sudo apt install make nasm qemu-system-x86 dosfstools mtools bochs bochs-sdl bochsbios vgabios
```

Any text editor works - the original notes for this project were written using [micro](https://micro-editor.github.io/):

```bash
sudo snap install micro --classic
```

## Building and running

Clone the repo, then from the project root:

```bash
# Build the bootloader, kernel, and floppy image
make

# Boot the image in QEMU
./run.sh
```

If everything worked, a QEMU window opens, the BIOS briefly tries (and fails) to boot from a hard disk - which is expected, since no hard disk is attached - falls back to the floppy, and prints `Hello world!` to the screen. That message is only printed *after* a successful `INT 13h` disk read, so seeing it confirms the bootloader actually read real data off the emulated floppy disk rather than running purely static code.

To rebuild from a clean state:

```bash
make clean
make
```

### Debugging

**Bochs** (recommended for beginners - has a built-in GUI debugger with register/memory views):

```bash
./debug.sh
```

**QEMU + GDB** (for scriptable, breakpoint-driven debugging):

```bash
qemu-system-i386 -drive file=build/main_floppy.img,format=raw,if=floppy -s -S
```

`-S` pauses the CPU on startup and `-s` opens a GDB remote stub on port `1234`. In another terminal:

```bash
gdb
(gdb) target remote localhost:1234
(gdb) break *0x7C00      # entry point of the boot sector
(gdb) continue
```

From there you can single-step, inspect registers (`info registers`), and dump memory (e.g. `x/16xb 0x7e00`) to watch the disk read happen instruction by instruction. Note that exact breakpoint addresses inside `boot.asm` shift whenever the source changes - re-disassemble the freshly built binary to find them again:

```bash
ndisasm -b16 -o0x7C00 build/bootloader.bin
```

## Roadmap

Following along with the playlist, next steps are expected to include:

- [ ] Parsing the FAT12 directory structure to actually locate and load `kernel.bin` by name (rather than a fixed sector)
- [ ] Switching from 16-bit real mode to 32-bit protected mode
- [ ] A minimal C kernel entry point
- [ ] Basic drivers (screen, keyboard)

## Credits

This project is based on the [x86 OS Development YouTube playlist](https://www.youtube.com/playlist?list=PLFjM7v6KGMpiH2G-kT781ByCNC_0pKpPN), used as the starting point and learning foundation. All code in this repo was written by hand alongside the videos, with personal notes kept in [`theory/`](./theory), and is being progressively customized and extended beyond what the series covers as the project evolves.

## License

This is an educational project, released under the [MIT License](./LICENSE) - free to use, study, and build on for learning purposes.
