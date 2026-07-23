# 海盗船 compatibility notes

## Container

Observed local image:

```text
magic             D300
container size    0x172f0 (94960)
header size       0x80
icon offset       0x100
icon size         0x210 (528)
program offset    0x310
program size      0x16b08 (92936)
resource offset   0x16e18
resource size     0x4d8 (1240)
guest load base   0x02700000
```

The appended resource is an HTML build log from the original packaging tool,
not game content. The title/board artwork is embedded in the program image.

## Known external behavior

- Window class string: `PirateshipApp`.
- Uses five save slots named `海盗船存档一` through `海盗船存档五`, with `.sav`
  suffixes.
- Save root in the original app: `A:\系统\数据\`.
- Uses a periodic GUI timer. The app-only compatibility trace observes
  `SetTimer(hwnd, 1, 20)` during `MSG_CREATE`; timer messages use
  `MSG_TIMER = 0x0144`.
- Inputs: up, down, left, right, confirm, and exit.
- The game can run without audio as an initial compatibility milestone.

## Implemented runtime path

- C runtime allocation, memory, and string helpers.
- GUI message loop and main-window lifecycle.
- GUI timer calls and tick count.
- Packed 2bpp drawing, virtual-screen blit, text, lines, and rectangles.
- Four-level grayscale conversion to the 9588 RGB565 framebuffer.
- Direct QEMU 9588 key-event translation for direction, confirm, and exit.

Save-file translation and audio remain future extensions. They are optional for
an interactive game session.

## Verified startup trace

With the SDK configuration used by the original application, the portable
probe executes the entry point and reaches these GUI slots:

| Slot | API | Observed role |
| ---: | --- | --- |
| 191 | `GetSysPixelIndex` | initial background color |
| 33 | `CreateMainWindow` | installs callback at guest `0x0270013c` |
| 388 | `GetBackPlayStruct` | optional background audio state |
| 192 | `GetGDCapability` | queries 2bpp display |
| 107 | `SetTimer` | timer id 1, interval 20 |
| 193/195 | `GetDC` / `ReleaseDC` | frame update |
| 29 | `SetInstantPaint` | frame batching |
| 336 | `ClrScr` | first timer frame |
| 35 | `DefaultMainWinProc` | unhandled messages |
| 12/95 | `GetMessage` / `MainWindowCleanup` | loop and shutdown |

The trace completes after 1,471 interpreted instructions when the headless
probe intentionally returns no queued messages.

## 9588 emulator verification

The native BDA embeds the locally supplied D300 bytes at build time; generated
game data stays under ignored `build/` and is never committed. The runtime maps
the program at its original `0x02700000` address and executes it unchanged.

Verified in the 9588 emulator:

1. Launching the BDA reaches the original 海盗船 title image.
2. The confirm key enters the two-board game screen.
3. A direction key changes the active board state.
4. The VM remains in the guest `GetMessage` loop between inputs, with timer
   messages serviced by the host adapter.

Because starting a BDA occurs inside the firmware's own event dispatch stack,
calling the native GUI poller recursively is unsafe. This port reads the
emulator's uncached diagnostic event mirror at `0xA9F00040` and writes its
160×240 output directly into the rotated 240×320 scanout at `0xA1F82000`.
