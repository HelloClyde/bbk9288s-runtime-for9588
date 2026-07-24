# BBK 9588 port

The adapter is a native BDA that opens the public SDK's firmware file selector
at startup. It reads the selected D300 EXE from the 9588 NAND, allocates the
guest address space, loads the original program at `0x02700000`, installs
relocation-table traps, and runs the portable C33 interpreter. No game bytes
are embedded in the BDA.

The current QEMU-specific host adapter provides:

- an unscaled 160×240 guest surface at the top center of the rotated 240×320
  RGB565 display;
- side EXE/settings buttons, a centered direction pad, and 取消/确认 actions;
- row-aligned packed 2bpp image decoding;
- a persistent 9288S GUI message loop and timer;
- direction/confirm/exit key translation;
- touch down, move, and up translation through QEMU's touch-state mirror;
- the public SDK's firmware Help Page, confirmation, window-destruction, and
  clean-exit handling;
- nested save/load window emulation with resumable guest callbacks;
- five persistent NAND save slots at `A:\PIRATE1.SAV` through
  `A:\PIRATE5.SAV`.

Build from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-bda.ps1
```

Start the LAN/touch-enabled 9588 frontend with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-9588-lan.ps1
```

The public SDK is pinned as the repository's `sdk` Git submodule and is used
only as a build dependency. The current direct scanout and diagnostic input
mirrors are emulator interfaces; physical 9588 hardware would require a
separate host adapter.
