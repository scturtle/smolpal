# smolpal (pal98 remake)

基于 [vb4_pal_decompiler](https://github.com/scturtle/vb4_pal_decompiler) 的反编译产物，用 C/C++ 忠实复现的《仙剑奇侠传》Windows 98 版，**在终端里运行**。

仅支持[98 版（百游）资源](https://archive.org/download/pal1_1995/XJ98rqp.zip)。

## 特性

- **零 SDL/SDLPAL 依赖**：源自 SDLPAL 精简版，平台层已完全自持，不链接 SDLPAL 树任何代码
- **视频**：[kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
- **键盘**：[kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/)
- **音频**：[miniaudio](https://github.com/mackron/miniaudio) + [adplug](https://github.com/adplug/adplug)
- **AVI**：自持解码 MS Video 1 + PCM u8 音轨，RGB555 真彩直呈
- **文本**：FreeType（内嵌 simsun.ttf）+ libiconv（GBK→Unicode）

## 构建

依赖：C99/C++17 编译器、freetype2、iconv（glibc 内建；macOS 需 libiconv）。

```bash
git clone --recurse-submodules <url> && cd sdlpal
make
```

## 运行

资源解压到 `pal98/` 目录（或任意目录，用环境变量指定），需要 kitty 终端：

```bash
PAL98_DATA=pal98 ./pal
```

常用环境变量：

| 变量 | 作用 |
|---|---|
| `PAL98_DATA=<dir>` | 数据目录（缺省 CWD；读模式大小写回退） |
| `PAL_KITTY_SCALE=1..4` | 终端帧整数像素放大 |
| `PAL_SKIP_AVI=1` | 跳过开机/开场/新游戏 AVI |
| `PAL_HEADLESS=1` | 强制无头模式（自动化） |

## 源码结构

| 文件 | 职责 |
|---|---|
| `pal.c`/`pal.h` | 游戏核心：全局状态、脚本 VM、场景、菜单、战斗 |
| `pal_ext.c` | PAL.dll/VB 运行时 bridge：视频合成、文本、文件 I/O、事件泵、RNG、YJ_1 解压 |
| `kitty.c`/`kitty.h` | 终端后端：graphics/keyboard protocol、时钟、终端生命周期 |
| `avi.c`/`avi.h` | AVI 容器解析 + MS Video 1 解码 + 真彩直呈 |
| `audio.cpp` | 音频后端：miniaudio 设备 + adplug RIX + WAV 音效 |
| `simsun_font.S` | 汇编 `.incbin` 内嵌 simsun.ttf |
