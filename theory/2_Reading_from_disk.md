
## 1. The problem: 512 bytes is not enough

- So far the OS has been limited to the **first sector** of a floppy (512 bytes), which is very little space.
- The priority now is to write code that **loads the rest of the OS into memory**, splitting the OS into **two modules**: the first loads the second.
- All operating systems are split this way because 512 bytes can't fit even the most basic OS functions.

## 2. What a bootloader does

The first module is the **bootloader**. Generally it:

- loads the most essential components of the OS into memory,
- puts the computer in the state the kernel expects,
- collects basic information about the system.

Details:

- Older OSes like **MS-DOS** ran in **16-bit real mode** (the mode used now), so the bootloader just had to load a binary and run it.
- More modern OSes expect the bootloader to switch to **32-bit protected mode** and gather system info.
- A key limitation of 32-bit protected mode: the **BIOS functions from Part 1 can no longer be used**. Some are important — e.g., a function that reports the **memory layout** (which memory is safe vs. reserved by hardware). Since these can't be called once in protected mode, the bootloader must collect all required info **before** starting the kernel. ("Not possible" here means possible only after a lot of setup, which comes too late.)

## 3. Why floppy disks 

A floppy disk is chosen because:

- it's the **simplest form of disk storage**,
- it's **universally supported** by all BIOSes and virtualization software,
- disk images are **easy to create and manipulate**,
- the **FAT12** file system is rudimentary and simple.

This makes it ideal for learning, before moving to other storage devices later.

### Disk layout options

- The simplest layout: bootloader in the **first sector (boot sector)**, rest of the OS from **sector 2** onward. Easy — the bootloader just reads sectors into memory and runs them.
- Problem: this leaves **no room for storing files**. Better to use an existing standard file system like **FAT, ext, or NTFS** so data can be exchanged with Windows/Linux.

## 4. How data is laid out on disks (CHS vs LBA)

Applies to all disks (floppy, CD, DVD, hard drive):

- Dividing the platter into **rings** → each ring is a **track** (or **cylinder**).
- Dividing it into **pizza slices** → each slice is a **sector**.
- Data can be stored on **both sides** of the platter; each side is a **head**. Hard disks may have multiple platters, so each side of each platter is a head.

**CHS (Cylinder-Head-Sector)** addressing tells the controller where data is via cylinder + head + sector. It makes sense physically but isn't convenient for us — we only care about position (beginning/middle/end), not physical location.

**LBA (Logical Block Addressing)** uses a **single number** to reference a block. It's what we want, but the **BIOS function only supports CHS**, so we must convert LBA → CHS ourselves.

Note: modern disks only *pretend* to have cylinders/heads/sectors for compatibility; they track physical location their own way.

### Conversion formulas

- In CHS, **cylinder and head are indexed from 0**, but **sector starts from 1**.
- Two constants: **sectors per track** (per side) and **heads per cylinder** (number of faces).
- **Sector** = (LBA % sectors_per_track) + 1
- **Head** = (LBA / sectors_per_track) % heads_per_cylinder
- **Cylinder** = (LBA / sectors_per_track) / heads_per_cylinder

### Bochs setup

- **Bochs** is an x86 emulator **and debugger**, useful for debugging the bootloader.
- Config file settings: **128 MB RAM**, paths to the **BIOS ROM** and **VGA ROM** images, **floppy** drive set to the disk image with `status=inserted`, **mouse disabled**, and **display library = SDL with the GUI debugger** option.
- A **`debug.sh`** script runs Bochs with this config.
- Install issues encountered: Bochs wasn't installed; also needed **bochs-sdl** (UI), **bochsbios**, and **vgabios** (ROMs). Then an error that **SDL wasn't available** — fixed by setting the display library to **`sdl2`** instead of `sdl`.
