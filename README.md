# BBK 9288S 应用兼容运行时（9588 版）

这是一个独立研究项目，用于在 BBK 9588 原生 `BDA` 中运行部分 BBK
9288S `D300` 应用。

本项目与 9288S 全系统模拟器、9588 硬件模拟器项目相互独立。
[9588 原生 BDA SDK](https://github.com/HelloClyde/bbk9588-bda-sdk)
以 Git 子模块形式固定在 `sdk/` 目录。本仓库不包含固件或原始应用程序。

## 工作原理

本项目实现的是用户态兼容层，而不是另一套完整机器模拟器：

```text
9288S D300 EXE
    -> S1C33 指令解释器
    -> 9288S 重定位表 API 陷阱
    -> 9588 BDA 图形 / 输入 / 定时器 / 文件系统适配层
```

运行时模拟 9288S 应用地址空间及 C33 ABI。9288S 程序通过重定位表调用
系统 API 时，兼容层会拦截调用并转换为 9588 原生服务。项目不模拟 NAND、
LCDC、ADC、GPIO 或完整的 9288S 内核。

这些应用采用的 GNU S1C33 ABI 使用 `R6` 至 `R9` 传递前四个参数，并通过
`R4` 返回标量值。间接调用重定位表函数时，`R4` 通常先保存目标地址，再被
函数返回值覆盖。

## 首个兼容目标：海盗船

经检查，9288S 版《海盗船》具有以下特征：

- `D300` 容器总大小为 94,960 字节；
- 应用图标占 528 字节；
- 程序映像占 92,936 字节，加载地址为 `0x02700000`；
- 尾部附带 1,240 字节构建日志，运行时不需要；
- 美术和游戏数据均嵌入程序映像；
- 外部数据仅为系统数据目录中的五个可选 `.sav` 存档文件。

因此它很适合作为第一个兼容目标。首阶段目标包括标题画面、棋盘绘制、方向/
确认/退出输入、定时器以及存档。

## 仓库结构

```text
docs/                  架构设计及各应用兼容说明
runtime/include/       可移植 D300/C33 运行时接口
runtime/src/           可移植运行时实现
ports/bbk9588/         9588 BDA 宿主适配层
assets/sanguo/         非可执行的存档容器初始化数据
scripts/               构建、测试、安装及局域网辅助脚本
tests/                 宿主机单元测试
tools/                 本地 D300 分析工具
sdk/                   固定版本的 9588 SDK 子模块
local/                 本地原始 EXE（Git 忽略）
build/                 构建产物（Git 忽略）
.github/workflows/     Tag 构建及 GitHub Release 自动化
```

## 当前进度

原版 9288S《海盗船》和《三国霸业》已能在基于 QEMU 的 9588 模拟器中交互运行：

- BDA 启动后先打开 9588 原生文件选择器，仅显示 `.exe`，并从 NAND 动态加载
  选中的 D300 映像；
- 可移植 S1C33 解释器直接执行未经修改的 D300 程序映像；
- 兼容运行时承接 9288S 重定位表 API 调用；
- 标题图、游戏棋盘和地图可以正确绘制到 9588 RGB565 帧缓冲；
- 确认键可以进入游戏，四个方向键可以改变游戏状态；
- Web 前端触摸输入会转换为 9288S 指针消息格式；
- 原版“帮助说明”和“退出游戏”触摸按钮可用，并使用 9588 原生帮助页及
  确认对话框承接；
- 原版存档/读档界面支持触摸，以 9288S 样式绘制，并将五个槽位持久化到
  9588 NAND；
- 分数、生命和关卡文字使用原版黑字白底的 DC 状态；
- 160×240 游戏画面不缩放，位于屏幕上方居中；左右为 EXE 和设置按钮，
  下方中央为方向键，左侧为“取消”，右侧为“确认”；
- 定时器及可恢复的 9288S 消息循环运行在原生 9588 BDA 内；
- 模拟器按键事件由兼容层直接消费，不会重新进入 9588 固件 GUI 事件分发器。

《三国霸业》还与真实 9288S 模拟器进行了逐项对照：

- 原生分辨率为 160×240，在 9588 上按像素原样显示；
- 标题、菜单、势力形势图及战略地图能够正确绘制；
- 当源矩形越过右边缘两个像素时，`SysShowPicV` 使用与 9288S 一致的裁剪行为；
- 连续移动不会残留旧像素或产生拖影；
- 主菜单第二项能够读取存档并返回战略地图；
- 游戏写入 `A:\SANGO0.SAV` 和 `A:\SANGO1.SAV`，游戏内覆盖存档后，
  完整重启 QEMU 仍能逐字节保持一致。

当前移植主要面向本项目配套的 QEMU 9588 模拟器，使用了诊断输入队列和直接
LCD 帧缓冲接口。要在真实 9588 硬件上运行，还需要实现不同的宿主适配层。
声音尚未实现，但不影响游玩和存档。EXE 选择及加载流程是通用的，API 覆盖仍需
按应用逐步扩充。

## 构建、安装与运行

环境要求：

- Windows PowerShell；
- Python 3.10 或更高版本；
- 9588 模拟器前端运行在 `http://127.0.0.1:8013`；
- 一个或多个你有权使用的 9288S D300 可执行文件。

克隆仓库并初始化 SDK 子模块：

```powershell
git clone https://github.com/HelloClyde/bbk9288s-compat9588.git
cd bbk9288s-compat9588
git submodule update --init sdk
```

已有仓库只需执行：

```powershell
git submodule update --init sdk
```

SDK 自己附带的可选模拟器子模块不是本项目的构建依赖，无需初始化。

构建 BDA：

```powershell
.\scripts\build-bda.ps1
```

第一次构建时，SDK 会下载带固定校验值的 MIPS 工具链，并存放到 Git 忽略的
`sdk/.toolchain/` 目录。生成文件位于：

```text
build/bbk9588/9288SCompat.bda
```

构建并安装到正在运行的 9588 模拟器：

```powershell
.\scripts\install-and-play.ps1 -GamePath "C:\path\to\海盗船.exe"
```

如果 EXE 已保存为 `local\pirate_ship.exe`，可以省略 `-GamePath`。脚本会：

1. 构建兼容 BDA；
2. 备份原来的 `宠物单词.bda`；
3. 以固定启动器文件名安装兼容运行时；
4. 将可选的 9288S EXE 复制到 9588 NAND 根目录；
5. 重置模拟器并进入“背单词/E-pets”；
6. 打开原生 EXE 文件选择器。

BDA 本身不包含任何游戏可执行文件。

安装器还会在《三国霸业》存档文件不存在时，通过 9588 SDK/NAND 文件路径创建
两个存档容器。已有的 `SANGO0.SAV` 和 `SANGO1.SAV` 始终会被保留，不会覆盖。
这是因为 9588 固件在应用首次创建文件时可能丢失 FAT 元数据；预先建立容器后，
《三国霸业》即可在游戏中正常覆盖、读取，并跨冷启动保存。

## 操作方式

9588 Web 前端控制：

| 游戏操作 | 键盘 | 屏幕按钮 |
| --- | --- | --- |
| 移动 | `W` `A` `S` `D` | 中央方向键 |
| 确认 | `J` | 右侧“确认” |
| 退出/返回 | `K` | 左侧“取消” |

点击左侧 `EXE` 按钮可退出当前程序并重新打开固件文件选择器。点击右侧 `SET`
按钮可切换底部控制区的左右手布局。

9588 Web 画面支持触摸。在《海盗船》游戏画面中：

- 点击“帮助说明”会打开 9588 固件 Help Page，包括滚动条和返回控件；
- 点击“存储文档”或“读取文档”可使用五个持久化槽位；
- 点击“退出游戏”会打开 9588 原生是/否确认对话框。

## 存档映射

《海盗船》的原始 GBK 长文件名会映射到以下 9588 NAND 文件：

| 原始槽位 | 9588 NAND 文件 |
| --- | --- |
| 海盗船存档一 | `A:\PIRATE1.SAV` |
| 海盗船存档二 | `A:\PIRATE2.SAV` |
| 海盗船存档三 | `A:\PIRATE3.SAV` |
| 海盗船存档四 | `A:\PIRATE4.SAV` |
| 海盗船存档五 | `A:\PIRATE5.SAV` |

《三国霸业》使用：

| 9288S 文件 | 9588 NAND 文件 |
| --- | --- |
| `SANGO0.SAV` | `A:\SANGO0.SAV` |
| `SANGO1.SAV` | `A:\SANGO1.SAV` |

恢复被替换的原始启动器 BDA：

```powershell
.\scripts\restore-original-bda.ps1
```

## 局域网访问

设置 9588 模拟器发行包路径并启动局域网前端：

```powershell
$env:BBK9588_EMULATOR_ROOT = "C:\path\to\bbk9588-emulator"
.\scripts\start-9588-lan.ps1
```

启动脚本会同时启用兼容 BDA 所需的 QEMU 触摸状态镜像。前端在所有 IPv4
接口的 TCP 8013 端口监听，并输出可用的局域网 URL。

首次使用时，请在管理员 PowerShell 中执行防火墙辅助脚本：

```powershell
.\scripts\enable-9588-lan-firewall.ps1
```

该规则仅允许专用网络、本地子网、TCP 8013 及模拟器自带的 Python 可执行文件。
两个局域网脚本也支持通过 `-ReleaseRoot` 参数直接传入模拟器目录。

## 分析与测试

检查本地 D300 应用：

```powershell
python .\tools\d300_inspect.py .\local\pirate_ship.exe --strings
```

运行宿主机测试：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\test-host.ps1
```

探测可移植核心执行本地 D300 映像的进度：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-d300.ps1 `
  -Image .\local\pirate_ship.exe
```

## 通过 Tag 自动发布

仓库包含 `.github/workflows/release.yml`。每次向 GitHub 推送 Tag 时，工作流会：

1. 运行 Python 测试及可移植 C 运行时测试；
2. 检出固定版本的 `sdk` 子模块；
3. 在 Windows 环境构建并校验 `9288SCompat.bda`；
4. 上传 BDA 及 `SHA256SUMS.txt` 作为工作流产物；
5. 创建或更新与 Tag 同名的 GitHub Release。

例如：

```powershell
git tag v0.1.0
git push origin v0.1.0
```

也可以在 GitHub Actions 页面手动运行工作流。手动运行只生成工作流产物，
不会创建 GitHub Release。
