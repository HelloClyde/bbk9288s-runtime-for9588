# BBK 9588 port

The adapter is a native BDA that scans
`A:\应用\数据\9288s\系统\程序`, validates each D300 EXE, reads its
embedded title and two-state 32x32 icon, and draws its own touchable program
list. It then reads the selected EXE from the 9588 NAND, allocates the guest
address space, loads the original program at `0x02700000`, installs
relocation-table traps, and runs the portable C33 interpreter. No game bytes
are embedded in the BDA.

The current 9588 host adapter provides:

- an unscaled 160×240 guest surface at the top center of the portrait 240×320
  RGB565 display;
- side EXE/settings buttons, a centered direction pad, and 取消/确认 actions;
- row-aligned packed 2bpp image decoding;
- a persistent 9288S GUI message loop and timer;
- native Frame presentation through the public 9588 drawing APIs, including
  aspect-preserving bilinear RGB565 213x320 portrait fullscreen scaling
  for the 160x240 guest surface;
- physical direction/confirm/exit input through the six-byte input packet;
- touch down, move, and up translation through the public raw-input stream
  and calibrated logical-coordinate API, matching the GBA 9588 port;
- the public SDK's firmware Help Page, confirmation, window-destruction, and
  clean-exit handling;
- nested save/load window emulation with resumable guest callbacks;
- a generic 9288S filesystem root mapped to
  `A:\应用\数据\9288s`, preserving the guest directory hierarchy.

Build from the project root:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-bda.ps1
```

Start the LAN/touch-enabled 9588 frontend with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\start-9588-lan.ps1
```

The public SDK is pinned as the repository's `sdk` Git submodule and is used
only as a build dependency. The runtime does not depend on emulator-private
framebuffer, key-queue, or touch-mirror addresses.
