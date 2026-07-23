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

The original 9288S 海盗船 executable now runs interactively in the QEMU-based
9588 emulator:

- the portable S1C33 interpreter executes the unmodified D300 program image;
- the 9288S relocation-table calls are handled by the compatibility runtime;
- the title artwork and two-board game view render to the 9588 RGB565 scanout;
- confirm enters the game and the four direction keys change game state;
- the timer and persistent 9288S message loop run inside the native 9588 BDA;
- emulator key events are consumed directly, without re-entering the 9588
  firmware GUI event dispatcher.

The current port deliberately targets the project's QEMU 9588 emulator. It
uses its diagnostic input queue and direct LCD scanout; physical 9588 hardware
would need a different host adapter. Sound and persistent `.sav` files are
not implemented yet, but they are not required to play a session.

## Build, install, and play

Prerequisites:

- the 9588 emulator frontend running at `http://127.0.0.1:8013`;
- the 9588 SDK/reference checkout at `E:\eebbk9588`;
- an authorized copy of the original 海盗船 D300 executable.

Run the complete workflow:

```powershell
cd E:\bbk9288s-compat9588
.\scripts\install-and-play.ps1 -GamePath "D:\path\to\海盗船.exe"
```

If the executable is at `game\pirate.exe` or at the locally discovered
original path, `-GamePath` can be omitted. The script builds the BDA, backs up
the original `宠物单词.bda`, installs the compatibility runtime under that
fixed launcher name, resets the emulator, selects 背单词/E-pets, and opens
海盗船.

Controls in the 9588 web frontend:

| Game input | Keyboard | Frontend button |
| --- | --- | --- |
| Move | `W` `A` `S` `D` | direction pad |
| Confirm | `J` | 确定 |
| Exit/back | `K` | 退出 |

Restore the overwritten launcher BDA:

```powershell
.\scripts\restore-original-bda.ps1
```

Expose the currently packaged 9588 emulator to the private LAN:

```powershell
.\scripts\start-9588-lan.ps1
```

The frontend then listens on TCP 8013 on all IPv4 interfaces. Run the firewall
helper once from an elevated PowerShell prompt; its inbound rule is limited to
the Private profile, the local subnet, TCP 8013, and the emulator's bundled
Python executable:

```powershell
.\scripts\enable-9588-lan-firewall.ps1
```

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
