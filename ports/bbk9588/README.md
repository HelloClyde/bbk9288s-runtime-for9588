# BBK 9588 port

The current adapter is a diagnostic BDA entry point. It looks for an authorized
local D300 application at:

```text
A:\pirate.exe
a:\pirate.exe
```

It allocates the guest IRAM/SDRAM, loads the D300 program at `0x02700000`,
installs relocation-table traps, and runs the portable interpreter. The
diagnostic adapter currently exercises main-window creation, `MSG_CREATE`, and
one timer callback with headless GUI stubs. It does not present the game
surface or run a persistent input/message loop yet.

Build from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-bda.ps1
```

The existing `E:\eebbk9588` SDK is used only as a build dependency.
