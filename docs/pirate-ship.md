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

## First API families to implement

- C runtime table: allocation/free and common memory/string helpers.
- GUI message loop and main-window lifecycle.
- GUI timer calls and tick count.
- Game drawing helpers (`SysShowPic*`, virtual-screen blit, text helpers).
- Message box for exit confirmation.
- Filesystem open/read/write/seek/close for save files.

The exact slot list will be finalized from app-only call traces before each trap
is marked supported.

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
