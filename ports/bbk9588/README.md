# BBK 9588 port

The adapter is a native BDA containing the authorized D300 application selected
at build time. It allocates the guest address space, loads the original program
at `0x02700000`, installs relocation-table traps, and runs the portable C33
interpreter.

The current QEMU-specific host adapter provides:

- a centered 160×240 guest surface on the rotated 240×320 RGB565 display;
- row-aligned packed 2bpp image decoding;
- a persistent 9288S GUI message loop and timer;
- direction/confirm/exit key translation;
- touch down, move, and up translation through QEMU's touch-state mirror;
- the public SDK's firmware Help Page, confirmation, window-destruction, and
  clean-exit handling.

Build from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-bda.ps1
```

Start the LAN/touch-enabled 9588 frontend with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-9588-lan.ps1
```

The public SDK at `C:\Users\ASUS\Documents\eebbk9588_native_sdk` is used only
as a build dependency. The current direct scanout and diagnostic input mirrors
are emulator interfaces; physical 9588 hardware would require a separate host
adapter.
