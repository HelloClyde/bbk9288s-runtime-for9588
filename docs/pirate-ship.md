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
- Direct QEMU touch-state translation to 9288S `MSG_LBUTTONDOWN`,
  `MSG_MOUSEMOVE`, and `MSG_LBUTTONUP` messages.
- Native 9588 Help Page for the original help text, plus a native message box
  for exit confirmation.
- Clean 9288S main-window destruction and quit-message handling.
- Wall-clock-paced guest timers using MiniGUI's 10 ms tick unit and the 9588
  SYS delay service.

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
| 336 | `PutImageArea` | full frames and 13×13 board cells |
| 174 | `MessageBox` | exit confirmation |
| 254 | `SaveScreenBox` | pre-dialog screen snapshot |
| 448 | `Help2` | help text dialog |
| 34/19 | `DestroyMainWindow` / `PostQuitMessage` | clean exit |
| 35 | `DefaultMainWinProc` | unhandled messages |
| 12/95 | `GetMessage` / `MainWindowCleanup` | loop and shutdown |

The scripted headless trace now enters the board, injects touch on the exit
button, accepts the yes/no confirmation, destroys the main window, and reaches
normal VM completion.

## 9588 emulator verification

The native BDA embeds the locally supplied D300 bytes at build time; generated
game data stays under ignored `build/` and is never committed. The runtime maps
the program at its original `0x02700000` address and executes it unchanged.

Verified in the 9588 emulator:

1. Launching the BDA reaches the original 海盗船 title image.
2. The confirm key enters the game board.
3. Repeated direction input changes game state without scanline corruption or
   stale movement trails.
4. Tapping `帮助说明` opens the original help text.
5. Tapping `退出游戏` opens a yes/no dialog and confirming returns cleanly to
   the 9588 desktop.
6. The VM remains in the guest `GetMessage` loop between inputs, with timer
   messages serviced by the host adapter.

Because starting a BDA occurs inside the firmware's own event dispatch stack,
calling the native GUI poller recursively is unsafe. This port reads the
emulator's uncached diagnostic event mirror at `0xA9F00040` and writes its
160×240 output directly into the rotated 240×320 scanout at `0xA1F82000`.

`PutImageArea` receives a 16-byte SDK image object header followed by its
packed 2bpp payload. For the full-screen image the header records width 160,
height 240, and payload length 9,600. Skipping this header is required before
decoding pixels; treating it as image data shifts every scanline and visibly
breaks the board at the middle.

Packed 2bpp rows are independently byte-aligned. A 13×13 cell therefore uses
a 4-byte row stride and a 52-byte payload, not a continuous 43-byte bitstream.
Both the source byte and its two-bit shift restart at each row. This detail is
what prevents moving cells from becoming skewed or leaving corrupted trails.

Touch does not appear in the regular QEMU key queue. The LAN launcher enables
the `touch-trace=on` machine option, and the BDA polls the uncached touch mirror
at `0xA9F00100`. Raw SADC coordinates are converted to the centered 160×240
guest surface before pointer messages are queued.

The original help action calls GUI slot 448, `Help2(hWnd, helpString)`.
The adapter reads the zero-terminated GBK help text from guest memory and
passes it to the public 9588 SDK call
`bda_help_page(parent, title, body)`. This invokes firmware GUI slot `+0x5A8`,
which owns the title bar, scrollable body, scrollbar, bottom return bar, and
its modal message loop. The compatibility BDA has no native parent Frame, so
it uses the SDK-verified `parent=0` form and restores the guest framebuffer
after the Help Page closes.

The guest `GetMessage` call yields when no message is available. Each empty
poll now waits 10,000 microseconds through the 9588 SYS delay service before
advancing the emulated timer by one 10 ms tick. MiniGUI's legacy `SetTimer`
argument is a tick count, not a millisecond count, so the original
`SetTimer(hwnd, 1, 20)` fires every 200 ms (approximately 5 Hz) instead of
being tied to QEMU CPU execution speed.
