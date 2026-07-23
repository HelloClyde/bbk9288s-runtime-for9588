# BBK 9288S application compatibility runtime for BBK 9588

This is a standalone research project for running selected BBK 9288S `D300`
applications inside a native BBK 9588 `BDA`.

It is intentionally separate from:

- `E:\eebbk9288s-qemu`, the full-system 9288S emulator.
- `E:\eebbk9588`, the 9588 native BDA SDK and hardware emulator.

Those projects are read-only references/build dependencies. No firmware or
original application is stored here.

## Architecture

The intended runtime is a user-mode compatibility layer, not another full
machine emulator:

```text
9288S D300 EXE
    -> S1C33 instruction interpreter
    -> 9288S relocation-table API traps
    -> 9588 BDA GUI / input / timer / filesystem adapters
```

The runtime emulates the 9288S application address space and C33 ABI. Calls
through the 9288S relocation tables are intercepted and translated to 9588
native services. NAND, LCDC, ADC, GPIO, and the 9288S kernel are not emulated.

The GNU S1C33 ABI used by these applications passes the first four arguments
in `R6` through `R9` and returns scalar values in `R4`. Indirect relocation
table calls commonly use `R4` as the call target before it is overwritten by
the return value.

## First target: 海盗船

The locally inspected application has these properties:

- `D300` container, 94,960 bytes total.
- 528-byte application icon.
- 92,936-byte program image linked at guest address `0x02700000`.
- 1,240-byte appended build-log resource that is not needed at runtime.
- Artwork and game data are embedded in the program image.
- External data is limited to five optional `.sav` files under the system data
  directory.

This makes it a good first compatibility target. The first milestone is title
screen, board rendering, direction/confirm/exit input, timer ticks, and save
files.

## Repository layout

```text
docs/                  design and per-application compatibility notes
runtime/include/       portable D300 and C33 runtime interfaces
runtime/src/           portable runtime implementation
ports/bbk9588/         9588 BDA host adapter
tests/                 host-side unit tests
tools/                 local D300 inspection helpers
local/                 local original EXE files (ignored)
build/                 generated artifacts (ignored)
```

## Current status

The standalone project currently has:

- a validated D300 parser and inspection tool;
- a portable S1C33 interpreter covering the application's startup path;
- the real S1C33 GNU ABI (`R6`-`R9` arguments, `R4` return value);
- generated 9288S relocation tables and an SDK slot-listing tool;
- synchronous guest window-procedure callbacks;
- a headless API probe that executes 海盗船 through `MSG_CREATE` and one
  `MSG_TIMER`, then exits the message loop cleanly;
- a buildable 9588 diagnostic BDA.

The game is not playable on 9588 yet. The remaining work is the persistent
message/timer loop, RGB565 presentation, key translation, drawing helpers,
save-file path translation, and any instructions reached by those paths.

Inspect a local application:

```powershell
python .\tools\d300_inspect.py .\local\pirate_ship.exe --strings
```

Run host tests:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-host.ps1
```

Probe how far the portable core executes a local D300 image:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-d300.ps1 `
  -Image .\local\pirate_ship.exe
```
