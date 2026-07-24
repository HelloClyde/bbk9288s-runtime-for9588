# Compatibility architecture

## Why API translation alone is not enough

9288S applications contain Epson S1C33 machine code. BBK 9588 applications use
little-endian MIPS32 code. A 9588 cannot execute a 9288S EXE directly even when
the operating-system API names are similar.

The compatibility runtime therefore has two mandatory layers:

1. An S1C33 CPU interpreter for application code.
2. A trap layer that implements the 9288S relocation-table APIs using 9588 BDA
   services.

## Guest memory

The initial model uses the application-visible portions of the 9288S map:

| Guest range | Purpose |
| --- | --- |
| `0x00000000..0x00003fff` | internal RAM and application stack |
| `0x02000000..0x027fffff` | SDRAM, system tables, application image, BSS/heap |
| `0x0f000000..0x0fffffff` | compatibility-runtime API trap addresses |

The `D300` program image is copied to `0x02700000`. The general relocation table
is created at `0x02000200`, matching the 9288S SDK.

## API traps

The guest sees ordinary function pointers in its ROS33, GUI, filesystem, audio,
C runtime, and dictionary tables. Each pointer is a reserved trap address. When
the interpreter fetches a PC in the trap range, the host adapter:

1. identifies the API group and slot;
2. decodes arguments from C33 registers/stack;
3. calls the corresponding 9588 service;
4. writes the result to the C33 return register;
5. performs a guest return.

This keeps unmodified EXE code and avoids emulating 9288S hardware peripherals.

For the GNU S1C33 ABI used here, arguments one through four are in `R6` through
`R9`; a scalar return value is placed in `R4`. A guest window procedure is
called synchronously by pushing a private return sentinel and running the same
interpreter until the callback returns.

## Filesystem root

The compatibility layer treats `A:\应用\数据\9288s\` on the 9588 as the
complete 9288S filesystem root. Absolute and relative guest paths are
normalized below that directory while preserving their original GBK path
components. No game or save filename is special-cased, and parent traversal
cannot escape the private root.

## Display/input policy

9288S content is portrait and four-gray-level. The initial 9588 adapter keeps a
160x240 guest surface, unscaled, at the top center of the rotated 240x320
RGB565 display. The 40-pixel side areas hold EXE/settings actions, and the
remaining 240x80 area below the guest holds a centered touch direction pad,
with 取消 on the left and 确认 on the right.

Direction, confirm, and exit input are translated to the original 9288S window
messages. Touch inside the centered guest viewport is mapped back to 9288S
coordinates; touch outside it is reserved for the compatibility controls.

## Milestones

1. Select, parse, and load D300 images from the 9588 NAND.
2. Execute the standard application startup to the first API trap.
3. Implement C runtime allocation/string/memory calls.
4. Implement window creation, message queue, drawing surface, timer, and input.
5. Reach the 海盗船 title and game board.
6. Add save-file path translation and persistence. (Implemented for 海盗船.)
7. Generalize API coverage for additional 9288S applications.
