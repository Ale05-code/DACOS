# Building an OS -  Part 1: "Hello World"


## 1. Tools needed

- **Text editor** -  any editor works; the author uses **micro** (chosen because it uses common keyboard shortcuts).
- **make** -  to build the project.
- **NASM** -  the assembler, used to assemble the assembly code.
- **Virtualization software** -  the author uses **QEMU**, but VirtualBox, VMware, or any other will do.
- **OS environment** -  the tutorial uses **Ubuntu**.
  - On **Windows**: use **WSL (Windows Subsystem for Linux)**; alternatively **Cygwin**, which has most of the required tools.
  - On **macOS**: the tools are available through **Homebrew**.

The project starts with a `src` folder containing a file called `main.asm`.

## 2. What assembly is

- The first part of the OS must be written in **assembly**; C comes later, but at this stage there is no choice.
- Assembly is the **human-readable interpretation of machine code**. High-level languages (C, C++) are compiled into machine code -  the language the processor understands -  through many steps (abstract syntax tree, heavy optimization).
- Assembly is much simpler: instructions are just converted into their machine-code representation by a tool called an **assembler**.
- An assembly instruction is made of a **mnemonic** (keyword) plus a number of **operands** (parameters) -  typically zero, one, or two.

### Processor differences

- Instructions differ between processor architectures: **x86** (laptops/desktops) vs **ARM** (smartphones/tablets).
- Even same-architecture processors can differ -  e.g., **SSE** was introduced in the Pentium III and did not exist in the Pentium II.
- Newer processors keep **backwards compatibility**, so old programs still run unmodified. x86 backwards compatibility reaches all the way back to the **8086** -  the first x86 CPU, from at least 40 years ago.
- **Conclusion:** this OS targets the **x86 architecture**, using the **x86 assembly language**.

## 3. How the computer boots

When you power on the computer, the **BIOS** runs tests, shows a logo, and then -  the important part -  starts the operating system. There are two ways the BIOS can load an OS:

1. **Legacy booting** -  the BIOS loads the first block/sector from each boot device into memory until it finds a certain **signature**; once found, it jumps to the first instruction of the loaded block. This is where our OS begins.
2. **EFI** -  the BIOS looks for a special **EFI partition** on each device containing special EFI programs.

This tutorial covers **legacy mode only** (EFI is not covered here).

### Plan

Write code -> assemble it -> place it in the **first sector of a floppy disk** -> add the signature the BIOS requires -> test.

## 4. Writing the boot code

### `org` directive

- The BIOS loads the OS at address **0x7C00**.
- The **`org`** directive tells the assembler to calculate all memory offsets starting at that address. It does **not** change where the BIOS loads the code -  it only affects how the assembler computes variables and labels.

### Directive vs instruction

- A **directive** gives the assembler a hint about how to interpret the code and is **not** translated into machine code.
- An **instruction** **is** translated into a machine-code instruction.

### `bits` directive

- Every x86 CPU must be backwards compatible with the 8086, so the CPU always **starts in 16-bit mode**.
- **`bits 16`** tells the assembler to emit 16-bit code (`bits 32` would emit 32-bit code). It is a directive -  it does not itself switch the processor's mode.

### The `main` label

- The `main` label marks where the code begins.
- To verify the BIOS loads correctly, only a **halt** instruction is written (it stops the processor).
- Because the CPU can sometimes resume, a second label is added with a **jump back to itself**, creating an infinite loop so the processor never runs past the end of the program (which would be unsafe).

## 5. Adding the boot signature

- The BIOS expects the **last two bytes of the first sector** to be **`0xAA55`**.
- The program is placed on a standard **1.44 MB floppy disk**, where one sector is **512 bytes**.
- **`DB`** (declare constant byte) can emit bytes directly.
- The **`times`** directive repeats instructions/data -  here it **pads** the program up to **510 bytes**.
- In NASM, **`$`** = position of the beginning of the current line, and **`$$`** = position of the beginning of the current section, so **`$ - $$`** = the length of the program so far in bytes.
- **`DW`** (declare word) declares the two-byte signature (a "word").

At this point the first OS is written -  it does nothing but stop the processor.

## 6. Building and testing

- A **`build`** directory keeps things organized.
- A **Makefile** is created with:
  - a rule to build `main.asm` with **NASM**, outputting a **binary** format;
  - a rule to build the disk image, taking that binary and **padding it with zeros to 1.44 MB**.
- Tested with **QEMU** (easy to set up, usable from the command line). The system boots from floppy and does **nothing** -  exactly as expected. "It does nothing, and does it perfectly."

## 7. x86 architecture concepts (before "Hello world")

### Registers

Registers are tiny, very fast pieces of memory built into the CPU:

- **General-purpose registers** -  usable for almost anything.
- **Index registers** -  usually hold indices and pointers.
- **Program counter** -  tracks the memory location where the current instruction begins.
- **Segment registers** -  track the currently active memory segments.
- **Flags register** -  holds special flags set by various instructions.
- A few more special-purpose registers, introduced only when needed.

### RAM and segmentation

- The 8086 had a **20-bit address bus** -> 2²⁰ ≈ **1 MB** of addressable memory (huge at the time, when machines had 64–128 KB).
- Intel used a **segment:offset** addressing scheme with two 16-bit values.
- Each **segment** covers **64 KB**, addressed by the **offset**; segments **overlap every 16 bytes**.
- Convert **segment:offset -> absolute address**: shift the segment 4 bits left (multiply by 16), then add the offset.
- Multiple segment:offset pairs can map to the **same absolute address** (e.g., 0x7C00).

### Segment registers

- **CS** -  code segment (where the processor executes code); **IP** (program counter) gives only the offset.
- **DS**, **ES** -  data segments.
- **FS**, **GS** -  additional data segments introduced by newer processors.
- **SS** -  stack segment.
- To use a segment you must load it into a register; **CS can only be changed by performing a jump**.

### Referencing memory

- Syntax: `segment_register : [ offset_expression ]`. The segment register can be omitted, in which case **DS** is used by default.
- The processor can do arithmetic inside the expression using **base**, **index**, **scale**, and **displacement** operands.
- **16-bit mode limitations** (due to the original 8086 design):
  - Only **BP** and **BX** can be **base** registers.
  - Only **SI** and **DI** can be **index** registers.
  - Constants **cannot** be written directly to segment registers -  an intermediary register is required.
- **32-bit mode** arrived with the **386**, largely making 16-bit mode obsolete; many newer features were never added to 16-bit mode. It still exists only for backwards compatibility, and its main modern use is the **startup sequence** -  most OSes switch to 32/64-bit mode right after starting (this series will too, in a later video).
- **scale** and **displacement** operands are numerical constants: **scale** works only in 32/64-bit mode and can be **1, 2, 4, or 8**; **displacement** can be any signed integer constant. All operands in a memory reference are **optional**.

#### Memory reference examples

- A label pointing to a word with value 100: moving the **offset** of the label vs. moving the **contents** at that label (DS used by default). Labels are simply constants pointing to a specific offset.
- Reading the **third element** of an array of words: put the array offset in **BX**, and since indexing is zero-based, the third element is `array[2]`; each word is 2 bytes, so **SI = 4**. The assembler evaluates constant expressions (e.g., `2 * 2`) at compile time. Note: something like `bx + ax*2` uses a register (`ax`) unknown at compile time -  allowed **only** inside a memory reference expression (the only place registers can appear in an expression). The element is fetched via `[BX + SI]`, where BX is the base and SI is the index.

## 8. Setting up segments and the stack

- The BIOS sets up **CS**, pointing to **segment 0** (standard behavior: **segment 0, offset 0x7C00**; some BIOSes use segment `0x07C0`, offset 0).
- **DS** and **ES** may not be initialized, so they must be set up. Since constants can't be written directly to segment registers, an **intermediary register (AX)** is used. The **`mov`** instruction copies from source (left) to destination (right).
- The **stack segment (SS)** is set to 0 and the **stack pointer (SP)** to the **beginning of the program**.

### The stack

- The stack is memory accessed in a **LIFO** manner via **`push`** and **`pop`**. (The narration says "first in first out," but push/pop behavior described is last-in-first-out.)
- It has a special role with **functions**: `call` pushes the **return address**; `ret` pops it and jumps back.
- The stack **grows downwards**: on `push`, **SP is decremented** by the number of bytes, then the data is written.
- It is placed at the **start of the OS** precisely because it grows downward -  putting it at the end would overwrite the program. The beginning is a safe spot.

## 9. The "print string" function

Goal: a function that receives a **pointer to a string in `DS:SI`** and prints characters until it hits a **null terminator**.

- Because the function is written **above `main`**, a **jump** is added above it so `main` remains the entry point.
- The function first **pushes** the registers it will modify.
- Main loop:
  - **`lodsb`** loads the byte at `DS:SI` into **AL** and increments **SI**.
  - **`or al, al`** performs a bitwise OR of AL with itself -  the value is unchanged, but it updates the **flags**; if AL is zero, the **zero flag** is set.
  - A **conditional jump** exits the loop (jumps to the "done" label) when the zero flag is set -  i.e., when the character is null.
  - *(The author notes a mistake in the recording: a jump back to the loop label was missing so the loop would repeat.)*
- After the loop, the pushed registers are **popped in reverse order**, then the function **returns**.

### Printing via the BIOS (interrupts)

- The **BIOS** (Basic Input/Output System) also provides basic functions such as writing text to the screen; these are called using **interrupts**.
- An **interrupt** is a signal that makes the processor stop what it's doing to handle an event. Three ways to trigger one:
  1. **Exception** -  generated by the CPU on a critical error (e.g., division by zero); OSes use these to stop or recover a misbehaving process.
  2. **Hardware** -  e.g., a keypress or the disk controller finishing a read.
  3. **`int` instruction** -  takes a parameter with the interrupt number (**0–255**).
- The BIOS installs interrupt handlers. Typically an interrupt number covers a **category** of functions, and the value in **AH** selects the specific function.
- To print text: call **`int 0x10`** (video services). Set **AH = 0x0E** for **teletype output**. Set **AL** = the ASCII character to print and **BH** = the page number (BL is only for graphics mode and can be ignored in text mode).
  - *(The author notes another recording mistake: forgetting to set the page number to zero.)*

### The string

- A "Hello world" string is added, followed by a **new line**, which requires printing both a **line feed** and a **carriage return**.
- A **macro** is created to avoid remembering the hex codes each time.
- The string is declared with **`DB`** (which allows writing many characters at once).
- Finally, **SI** is set to the string's address and the print function is called.

