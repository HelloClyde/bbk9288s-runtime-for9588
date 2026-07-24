# BBK 9288S application compatibility runtime for BBK 9588

This is a standalone research project for running selected BBK 9288S `D300`
applications inside a native BBK 9588 `BDA`.

It is intentionally separate from the full-system 9288S simulator and the
9588 hardware-emulator project. The
[public 9588 native BDA SDK](https://github.com/HelloClyde/bbk9588-bda-sdk)
is pinned under `sdk/` as a Git submodule. No firmware or original application
is stored here.

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
assets/sanguo/         non-executable save-container initialization fixtures
scripts/               build, test, emulator install, and LAN helpers
tests/                 host-side unit tests
tools/                 local D300 inspection helpers
sdk/                   pinned 9588 native SDK Git submodule
local/                 local original EXE files (ignored)
build/                 generated artifacts (ignored)
.github/workflows/     tag build and GitHub release automation
```

## Current status

The original 9288S 海盗船 and 三国霸业 executables now run interactively in the
QEMU-based 9588 emulator:

- launching the BDA first opens the native 9588 file selector, filtered to
  `.exe`, and loads the selected D300 image from NAND at runtime;
- the portable S1C33 interpreter executes the unmodified D300 program image;
- the 9288S relocation-table calls are handled by the compatibility runtime;
- the title artwork and game board render to the 9588 RGB565 scanout;
- confirm enters the game and the four direction keys change game state;
- web-frontend touch input is translated to the 9288S pointer-message format;
- the original 帮助说明 and 退出游戏 touch buttons work, including the
  native 9588 confirmation dialogs;
- the original save/load selectors support touch, render like the 9288S
  screens, and persist all five slots in the 9588 NAND;
- score, life, and level text uses the original black-on-white DC state;
- the 160×240 guest view is centered at the top without scaling, with EXE and
  settings buttons at its sides, a centered direction pad below it, 取消 on
  the left, and 确认 on the right;
- the timer and resumable 9288S message loops run inside the native 9588 BDA;
- emulator key events are consumed directly, without re-entering the 9588
  firmware GUI event dispatcher.

三国霸业 has additionally been checked against the real 9288S simulator:

- its native guest resolution is 160×240 and is displayed pixel-for-pixel;
- the title, menu, 势力形势图, and strategic gameplay map render correctly;
- `SysShowPicV` uses the 9288S clipping behavior when a source rectangle
  extends two pixels past the right edge;
- repeated movement leaves no stale pixels or screen trails;
- the second main-menu item loads a saved session back into the strategic map;
- the game writes `A:\SANGO0.SAV` and `A:\SANGO1.SAV`, and an in-game
  overwrite survives a complete QEMU restart byte-for-byte.

The current port deliberately targets the project's QEMU 9588 emulator. It
uses its diagnostic input queue and direct LCD scanout; physical 9588 hardware
would need a different host adapter. Sound is not implemented, but it is not
required to play or save a session. EXE selection/loading is generic; API
coverage is still expanded application by application.

## Build, install, and play

Prerequisites:

- Windows PowerShell and Python 3.10 or newer;
- the 9588 emulator frontend running at `http://127.0.0.1:8013`;
- one or more authorized 9288S D300 executables.

Clone and initialize the SDK submodule:

```powershell
git clone <repository-url>
cd bbk9288s-compat9588
git submodule update --init sdk
```

For an existing clone, initialize or update the SDK with:

```powershell
git submodule update --init sdk
```

The SDK's optional emulator submodule is not needed to build this project.

Build the BDA:

```powershell
.\scripts\build-bda.ps1
```

On the first build, the SDK downloads its checksum-pinned MIPS toolchain into
the ignored `sdk/.toolchain/` directory. The generated file is
`build/bbk9588/9288SCompat.bda`.

Run the complete emulator install workflow:

```powershell
.\scripts\install-and-play.ps1 -GamePath "C:\path\to\海盗船.exe"
```

If the executable is stored as `local\pirate_ship.exe`, `-GamePath` can be
omitted. The script builds the BDA, backs up the original `宠物单词.bda`,
installs the compatibility runtime under that fixed launcher name, copies the
optional EXE to the 9588 NAND root, resets the emulator, selects
背单词/E-pets, and opens the native EXE selector. The BDA itself contains no
game executable.

The installer also creates the two 三国霸业 save containers through the
9588 SDK/NAND file path when they are missing. Existing `SANGO0.SAV` and
`SANGO1.SAV` files are always preserved. This is required because the 9588
firmware can lose FAT metadata when a guest application creates those files
for the first time; once pre-created, 三国霸业 can overwrite and reload them
normally across cold starts.

Controls in the 9588 web frontend:

| Game input | Keyboard | Frontend button |
| --- | --- | --- |
| Move | `W` `A` `S` `D` | centered direction pad |
| Confirm | `J` | right-side 确认 |
| Exit/back | `K` | left-side 取消 |

Tap the left-side `EXE` button to leave the current guest and reopen the
firmware selector. Tap the right-side `SET` button to swap the bottom controls
between left- and right-handed layouts.

The 9588 web display is also touch-enabled. On the game screen, tap
`帮助说明` to open the firmware Help Page (including its scroll bar and return
controls), `存储文档` / `读取文档` to use one of the five persistent slots, or
`退出游戏` to open its yes/no confirmation.

The original long GBK save names are translated to these persistent 9588 NAND
files:

| Original slot | 9588 NAND file |
| --- | --- |
| 海盗船存档一 | `A:\PIRATE1.SAV` |
| 海盗船存档二 | `A:\PIRATE2.SAV` |
| 海盗船存档三 | `A:\PIRATE3.SAV` |
| 海盗船存档四 | `A:\PIRATE4.SAV` |
| 海盗船存档五 | `A:\PIRATE5.SAV` |

三国霸业 uses:

| 9288S file | 9588 NAND file |
| --- | --- |
| `SANGO0.SAV` | `A:\SANGO0.SAV` |
| `SANGO1.SAV` | `A:\SANGO1.SAV` |

Restore the overwritten launcher BDA:

```powershell
.\scripts\restore-original-bda.ps1
```

Expose the currently packaged 9588 emulator to the private LAN:

```powershell
$env:BBK9588_EMULATOR_ROOT = "C:\path\to\bbk9588-emulator"
.\scripts\start-9588-lan.ps1
```

This launcher also enables the QEMU touch-state mirror required by the
compatibility BDA. The frontend listens on TCP 8013 on all IPv4 interfaces
and prints the usable LAN URL. Run the firewall helper once from an elevated
PowerShell prompt; its inbound rule is limited to the Private profile, the
local subnet, TCP 8013, and the emulator's bundled Python executable:

```powershell
.\scripts\enable-9588-lan-firewall.ps1
```

Both LAN helpers also accept `-ReleaseRoot` instead of the environment
variable.

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

## GitHub tag releases

The repository includes `.github/workflows/release.yml`. Every pushed tag:

1. runs the Python and portable C runtime tests;
2. checks out the pinned `sdk` submodule;
3. builds and validates `9288SCompat.bda` on Windows;
4. uploads the BDA and `SHA256SUMS.txt` as workflow artifacts;
5. creates or updates the matching GitHub Release.

For example:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

The workflow can also be started manually from GitHub Actions; a manual run
produces workflow artifacts but does not create a GitHub Release.
