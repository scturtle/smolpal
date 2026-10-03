// NOTE: 实现 golden PAL.dll ABI（对应 pal_dll.c）的平台桥；呈现/输入层由
// kitty 终端后端取代 golden 的 GDI/DirectDraw/DirectInput。

#include "pal_ext.h"
#include "avi.h"
#include "kitty.h"
#include "pal.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <dirent.h>
#include <iconv.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// NOTE: golden 无 dst 上限；remake 增设 65536（=fire_mkf_data 容量，≥合法流
// 最大解码 65502 不剪合法数据，详见 remake.md §4）。
#define PAL_UNPAK_DST_CAP 65536

static void pal_blit_rle(uint8_t *dstbase, const uint8_t *bitmap, int x, int y, int shadow);

static void pal_events_pump(void);

static int pal_clip_left = 0, pal_clip_top = 0;
static int pal_clip_right = 319, pal_clip_bottom = 199;
static int pal_vwin_left = 0, pal_vwin_top = 0;
static int pal_vwin_right = 319, pal_vwin_bottom = 199;
static uint8_t *pal_render_page;
static uint8_t *pal_clip_page;
static int pal_clip_relx = 0, pal_clip_rely = 0;
static int pal_clip_n1 = 200;
static int16_t pal_wave_off[16];
static int16_t pal_wave_row_off[200];
static int16_t pal_wave_ent_a;
static int16_t pal_wave_ent_b;
static uint16_t pal_wave_phase;
static bool pal_wave_active;
static uint32_t pal_tick_base;
static int pal_tick_hold;
static bool pal_timer_running;

static uint32_t pv_lut[256];

static FT_Library pt_library;
static FT_Face pt_face;
static bool pt_font_failed;

extern const unsigned char pal_simsun_ttf_start[];
extern const unsigned char pal_simsun_ttf_end[];
static iconv_t pt_iconv = (iconv_t)-1;
static bool pt_iconv_failed;

typedef wchar_t WCHAR;
typedef const WCHAR *LPCWSTR;
typedef struct {
  int16_t x, y;
} PAL_POS;
#define PAL_XY(x, y) ((PAL_POS){(x), (y)})

#define PT_FONT_HEIGHT 16

static int pal_tick_now(void) {
  return pal_timer_running ? (int)((kitty_ticks_ms() - pal_tick_base) / 10) : pal_tick_hold;
}

static int pal_screen_dirty;

static void pal_present_direct(void) {
  pal_screen_dirty = 0;
  if (!kitty_video_up())
    return;
  // NOTE: remake 增补内容哈希去重（索引页 + 调色板 LUT 未变不重发）——golden
  // 靠 WM_PAINT 按需重绘，remake 无消息泵，防静态菜单灌满终端。
  static uint32_t lastSig = 0;
  const uint8_t *lut = (const uint8_t *)pv_lut;
  uint32_t h = 2166136261u;
  int i;
  for (i = 0; i < 64000; i++) {
    h ^= screen_surf[i];
    h *= 16777619u;
  }
  for (i = 0; i < 1024; i++) {
    h ^= lut[i];
    h *= 16777619u;
  }
  if (h == lastSig)
    return;
  lastSig = h;
  kitty_present_frame(screen_surf, pv_lut);
}

#define PAL_TREE_MAX 256

typedef struct {
  const uint8_t *bitmap;
  int16_t x;
  int16_t ground_y;
  uint16_t depth;
  uint16_t height;
} pal_tree_entry_t;

uint16_t PAL_SpriteHeight(const uint8_t *bitmap) {
  if (bitmap == NULL)
    return 0;
  if (bitmap[0] == 2 && bitmap[1] == 0 && bitmap[2] == 0 && bitmap[3] == 0)
    bitmap += 4;
  return (uint16_t)(bitmap[2] | (bitmap[3] << 8));
}

static pal_tree_entry_t pal_tree[PAL_TREE_MAX];
// NOTE: golden 把精灵段 splice 进 NIP 行列表、NipW* 拷贝时合成；remake 无行列表——
// 按拼接时点入队，NipWA/NipWB 背景拷贝后依序重放到段（行窗口裁剪）。
typedef struct {
  const uint8_t *bitmap;
  int32_t x;
  int32_t y;
  uint8_t isTree;
} pal_splice_entry_t;
// NOTE: golden 行列表超限 = 写越列表缓冲（相邻全局污染，UB）；remake 以
// PAL_SPLICE_MAX 封顶，容量 ≥ golden 自身存活阈值，不先于 golden 截断。
#define PAL_SPLICE_MAX 6000
static pal_splice_entry_t pal_splice[PAL_SPLICE_MAX];
static int32_t pal_splice_n = 0;

static void pal_splice_flush(uint8_t *dstseg, int rowShift, int winTop, int winBot, uint8_t nipwbEffect);
static uint16_t pal_tree_count;
static uint16_t pal_ntree_width;

// NOTE: depth/height 为 remake 扩展参数（golden AddToTree 无导出 height，
// 由 DLL 内部解析精灵头）；depth 镜像 golden 位管道，负值靠消费端恢复符号。
void PAL_QueueSprite(const uint8_t *bitmap, int16_t x, int16_t groundY, uint16_t depth, uint16_t height) {
  if (bitmap == NULL || pal_tree_count >= PAL_TREE_MAX)
    return;
  pal_tree[pal_tree_count].bitmap = bitmap;
  pal_tree[pal_tree_count].x = x;
  pal_tree[pal_tree_count].ground_y = groundY;
  pal_tree[pal_tree_count].depth = depth;
  pal_tree[pal_tree_count].height = height;
  pal_tree_count++;
}

void PAL_WaitTime(int16_t frames) {
  // NOTE: golden 忙等 10ms tick 计数器（阻塞 VB 消息泵）；remake 忙等每轮
  // 泵事件，长等待中可消化退出请求。
  while ((uint32_t)(int32_t)(int16_t)frames > (uint32_t)pal_tick_now()) {
    pal_events_pump();
    kitty_delay_ms(1);
  }
  pal_tick_hold = 0;
  pal_tick_base = kitty_ticks_ms();
}

void PAL_Delay(int16_t ticks) {
  // NOTE: 同 PAL_WaitTime：remake 忙等每轮泵事件（golden 阻塞消息泵）。
  pal_tick_hold = 0;
  pal_tick_base = kitty_ticks_ms();
  while ((uint32_t)(int32_t)(int16_t)ticks > (uint32_t)pal_tick_now()) {
    pal_events_pump();
    kitty_delay_ms(1);
  }
}

void PAL_SetTimer(void) {
  if (!pal_timer_running) {
    pal_tick_base = kitty_ticks_ms() - (uint32_t)pal_tick_hold * 10;
    pal_timer_running = true;
  }
}

void PAL_KillTimer(void) {
  if (pal_timer_running) {
    pal_tick_hold = (int)((kitty_ticks_ms() - pal_tick_base) / 10);
    pal_timer_running = false;
  }
}

void PAL_CopyMem(void *dest, const void *src, uint32_t count) { memcpy(dest, src, count); }

void PAL_PushScreen(uint8_t *buf) {
  if (buf)
    memcpy(buf, screen_surf, 64000);
}

void PAL_PopScreen(uint8_t *buf) {
  if (buf)
    memcpy(screen_surf, buf, 64000);
  PAL_Flip();
}

void PAL_ClearScreen(void) {}

void PAL_ClearMenu(void *ptr, uint16_t wordCount) {
  if (ptr != NULL)
    memset(ptr, 0, (size_t)wordCount * 2U);
}

void PAL_ClearTree(void) { pal_tree_count = 0; }
void PAL_ClearClipNA(uint16_t left, uint16_t top, uint16_t rowLimit, uint16_t rowWidth, uint16_t bottom, void *page,
                     void *list) {
  // NOTE: remake 无行列表：此处仅记录裁剪框/源页/行数，替代 golden 行列表构建；
  // 波形经 pal_wave_* 快照、精灵走拼接队列。
  int rows = (int)bottom - (int)top;
  (void)list;
  if ((int)rowLimit < rows)
    rows = rowLimit;
  if (rows < 0)
    rows = 0;
  if (rows > 200)
    rows = 200;
  pal_clip_page = (page != NULL) ? (uint8_t *)page : screen_surf;
  pal_clip_relx = left;
  pal_clip_rely = top;
  pal_clip_n1 = rows;
  pal_clip_left = left;
  pal_clip_top = top;
  pal_clip_right = (int)left + (int)rowWidth - 1;
  pal_clip_bottom = (int)top + rows - 1;
  if (pal_clip_right >= 320)
    pal_clip_right = 319;
  if (pal_clip_bottom >= 200)
    pal_clip_bottom = 199;
  pal_wave_active = false;
  pal_splice_n = 0;
}

void PAL_Flip(void) { pal_present_direct(); }

void PAL_AddToTree(int32_t x, int32_t footY, uint16_t depth, uintptr_t spritePtr) {
  // NOTE: golden 精灵引用为 32 位地址原样保存；remake 以 uintptr_t 传 64 位
  // 堆指针（uint32 形参会截断 → SEGV）。
  const uint8_t *bitmap = (const uint8_t *)(uintptr_t)spritePtr;
  PAL_QueueSprite(bitmap, (int16_t)x, (int16_t)footY, depth, PAL_SpriteHeight(bitmap));
}

void PAL_BlitSprite(const uint8_t *bank, uint32_t offset, int16_t y, int16_t x) {
  if (bank != NULL && offset < 0x1000000)
    pal_blit_rle(screen_surf, bank + offset, x, y, 0);
}

void PAL_BlitBitmap(const uint8_t *bitmap, int16_t y, int16_t x) { PAL_BlitBitmapTo(screen_surf, bitmap, y, x, 0); }

// NOTE: BlitSprite/BlitBitmap/BlitBitmapTo 三件套 golden 无对应导出且全仓
// 零调用（死桥面，保留）。
void PAL_BlitBitmapTo(uint8_t *dst, const uint8_t *bitmap, int16_t y, int16_t x, uint16_t shadow) {
  pal_blit_rle(dst, bitmap, x, y, shadow != 0);
}

static void pal_blit_rle_mode(uint8_t *dstbase, const uint8_t *bitmap, int x, int y, int mode, uint8_t effect,
                              int useClip, int winTop, int winBot) {
  int clipLeft, clipTop, clipRightEx, clipBottomEx;
  const uint8_t *src, *rle;
  uint8_t rowbuf[324];
  int w0, h0, rows, outW, leftSkip, dstOfs, remain, row, i;

  if (dstbase == NULL || bitmap == NULL)
    return;
  src = bitmap;
  w0 = src[0] | (src[1] << 8);
  h0 = src[2] | (src[3] << 8);
  src += 4;
  if (w0 <= 0 || w0 > 320 || h0 <= 0 || h0 > 200)
    // NOTE: remake 增补越界防护，golden 无此检查（安全超集）。
    return;
  rle = src;

  if (useClip == 1) {
    clipLeft = pal_vwin_left;
    clipTop = pal_vwin_top;
    clipRightEx = pal_vwin_right + 1;
    clipBottomEx = pal_vwin_bottom + 1;
  } else if (useClip == 2) {
    clipLeft = 0;
    clipTop = winTop;
    clipRightEx = 320;
    clipBottomEx = winBot;
  } else {
    clipLeft = 0;
    clipTop = 0;
    clipRightEx = 320;
    clipBottomEx = 200;
  }

  rows = h0;
  if (y < clipTop) {
    if (y + h0 <= clipTop)
      return;
    remain = (clipTop - y) * w0;
    rows = h0 - (clipTop - y);
    // NOTE: 忠实 golden（putp 0x2ea9）：do-while，literal 越界仍消费、
    // count==0 不提前退出（依赖每行 runs 合计==w0 的资源约定）。
    do {
      int countByte = *rle++;
      int count = countByte & 0x7F;
      if ((countByte & 0x80) == 0)
        rle += count;
      remain -= count;
    } while (remain > 0);
  }

  outW = w0;
  leftSkip = 0;
  if (x < clipLeft) {
    if (x + w0 <= clipLeft)
      return;
    leftSkip = clipLeft - x;
    outW -= leftSkip;
    x = clipLeft;
  }
  if (x + outW > clipRightEx) {
    outW -= (x + outW) - clipRightEx;
    if (outW <= 0)
      return;
  }
  if (outW <= 0)
    return;

  if (y < clipTop)
    y = clipTop;
  if (x < clipLeft)
    x = clipLeft;
  if (y + rows > clipBottomEx) {
    rows = clipBottomEx - y;
    if (rows <= 0)
      return;
  }
  dstOfs = y * 320 + x;
  for (row = 0; row < rows; row++) {
    const uint8_t *rp = rle;
    uint8_t *wp = rowbuf;
    memset(rowbuf, 0xFF, (size_t)((w0 + 3) & ~3));
    remain = w0;
    // NOTE: 忠实 golden（putp 0x2f8e）：跨界 run 仍消费全部数据字节
    // （流位置一致）；rowbuf 写入钳制为安全超集（golden 越界写不可观察）。
    do {
      int countByte = *rp++;
      int count = countByte & 0x7F;
      if (countByte & 0x80) {
        wp += count;
      } else {
        int k;
        for (k = 0; k < count; k++) {
          if (wp < rowbuf + sizeof(rowbuf))
            *wp = *rp;
          wp++;
          rp++;
        }
      }
      remain -= count;
    } while (remain > 0);
    rle = rp;
    const uint8_t *sp = rowbuf + leftSkip;
    uint8_t *dp = dstbase + dstOfs;
    for (i = 0; i < outW; i++) {
      uint8_t pix = sp[i];
      if (pix == 0xFF)
        continue;
      switch (mode) {
      case 1: {
        uint8_t low = (uint8_t)((pix & 0x0F) + effect);
        if ((int8_t)low < 0)
          low = 0;
        else if (low > 0x0F)
          low = 0x0F;
        dp[i] = (uint8_t)((pix & 0xF0) | low);
        break;
      }
      case 2: {
        int t = (pix & 0x0F) - (effect & 0x0F);
        dp[i] = (uint8_t)(t > 0 ? ((effect & 0xF0) | t) : 0);
        break;
      }
      case 3:
        dp[i] = (uint8_t)((dp[i] & 0xF0) | ((dp[i] & 0x0F) >> 1));
        break;
      case 4: {
        uint8_t nlow = (uint8_t)((pix & 0x0F) + effect);
        if (nlow < effect)
          dp[i] = (uint8_t)(pix & 0xF0);
        else if ((int8_t)nlow > 15)
          dp[i] = 0x0F;
        else
          dp[i] = (uint8_t)((pix & 0xF0) | nlow);
        break;
      }
      default:
        dp[i] = pix;
        break;
      }
    }
    dstOfs += 320;
  }
}

static void pal_blit_rle(uint8_t *dstbase, const uint8_t *bitmap, int x, int y, int shadow) {
  pal_blit_rle_mode(dstbase, bitmap, x, y, shadow ? 3 : 0, 0, 1, 0, 200);
}

void PAL_PutP(int x, int y, const uint8_t *sprite, void *target, uint32_t effect, int mode) {
  uint8_t *dst = (target == NULL) ? screen_surf : (uint8_t *)target;
  if (dst != NULL && sprite != NULL)
    pal_blit_rle_mode(dst, sprite, x, y, mode, (uint8_t)effect, mode == 0, 0, 200);
  // NOTE: golden p4==0 时逐次把脏矩形 blt 到主表面；remake 只标脏、由事件泵点
  // 统一 present（整帧原子替换，合并绘制突发防中间态闪烁）。
  if (target == NULL)
    pal_screen_dirty = 1;
}

void PAL_CvLong(uint16_t value, uint16_t *out) {
  if (out != NULL)
    *out = value;
}

void PAL_Ffxy(int16_t *x, int16_t *y, int16_t maxX, int16_t maxY) {
  if (*x < 0)
    *x = 0;
  if (*x > maxX)
    *x = maxX;
  if (*y < 0)
    *y = 0;
  if (*y > maxY)
    *y = maxY;
}

void PAL_ExPalate(uint8_t *dst, const uint8_t *src, uint16_t count, uint16_t step) {
  // NOTE: golden expate 无 768 字节上限；remake 增设冗余防护（安全超集）。
  int32_t n;
  int32_t cnt = (count > 768) ? 768 : count;
  for (n = 0; n < cnt; n++)
    dst[n] = (uint8_t)(((uint16_t)(src[n] * (uint16_t)step)) >> 6);
}

void PAL_IntPalate(uint8_t *pal) {
  int i;
  if (pal == NULL)
    return;
  for (i = 0; i < 256; i++) {
    uint32_t r = (uint32_t)pal[i * 3] << 2;
    uint32_t g = (uint32_t)pal[i * 3 + 1] << 2;
    uint32_t b = (uint32_t)pal[i * 3 + 2] << 2;
    pv_lut[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
  }
  pal_present_direct();
}

void PAL_FuPalate(uint16_t index, uint8_t *dst, const uint8_t *srcBase) {
  uint16_t off = (uint16_t)(index * 48);
  int i;
  for (i = 0; i < 16; i++)
    memcpy(dst + i * 48, srcBase + off, 48);
}

void PAL_CvPalate(uint8_t *moving, const uint8_t *target) {
  int n;
  for (n = 0; n < 768; n++) {
    uint8_t cur = moving[n], tgt = target[n];
    if ((int8_t)cur < (int8_t)tgt)
      moving[n] = (uint8_t)(cur + 1);
    else if ((int8_t)cur > (int8_t)tgt)
      moving[n] = (uint8_t)(cur - 1);
  }
}

void PAL_CorPalate(uint16_t index, uint8_t *pal) {
  // NOTE: golden 无界检查（index≥21846 偏移回绕，照读界外垃圾并广播）；
  // remake 拦 [256,21845] 越界读为 no-op（安全超集）。
  size_t offset = (size_t)(uint16_t)(index * 3);
  uint8_t c0, c1, c2;
  int i;
  if (pal == NULL || offset + 2 >= 768)
    return;
  c0 = pal[offset];
  c1 = pal[offset + 1];
  c2 = pal[offset + 2];
  for (i = 0; i < 256; i++) {
    pal[i * 3] = c0;
    pal[i * 3 + 1] = c1;
    pal[i * 3 + 2] = c2;
  }
}

#define YJ1_NODES 641
#define YJ1_ROOT 0x280
#define YJ1_EOS 0xFFF
#define YJ1_MAX_WEIGHT 0x8000

static uint16_t yj1_weight[YJ1_NODES];
static uint16_t yj1_value[YJ1_NODES];
static uint16_t yj1_parent[YJ1_NODES];
static uint16_t yj1_left[YJ1_NODES];
static uint16_t yj1_right[YJ1_NODES];
static uint16_t yj1_leaf[YJ1_NODES - 0x140];

static const uint8_t yj1_lookup_1[0x100] = {
    0x3f, 0x0b, 0x17, 0x03, 0x2f, 0x0a, 0x16, 0x00, 0x2e, 0x09, 0x15, 0x02, 0x2d, 0x01, 0x08, 0x00, 0x3e, 0x07, 0x14,
    0x03, 0x2c, 0x06, 0x13, 0x00, 0x2b, 0x05, 0x12, 0x02, 0x2a, 0x01, 0x04, 0x00, 0x3d, 0x0b, 0x11, 0x03, 0x29, 0x0a,
    0x10, 0x00, 0x28, 0x09, 0x0f, 0x02, 0x27, 0x01, 0x08, 0x00, 0x3c, 0x07, 0x0e, 0x03, 0x26, 0x06, 0x0d, 0x00, 0x25,
    0x05, 0x0c, 0x02, 0x24, 0x01, 0x04, 0x00, 0x3b, 0x0b, 0x17, 0x03, 0x23, 0x0a, 0x16, 0x00, 0x22, 0x09, 0x15, 0x02,
    0x21, 0x01, 0x08, 0x00, 0x3a, 0x07, 0x14, 0x03, 0x20, 0x06, 0x13, 0x00, 0x1f, 0x05, 0x12, 0x02, 0x1e, 0x01, 0x04,
    0x00, 0x39, 0x0b, 0x11, 0x03, 0x1d, 0x0a, 0x10, 0x00, 0x1c, 0x09, 0x0f, 0x02, 0x1b, 0x01, 0x08, 0x00, 0x38, 0x07,
    0x0e, 0x03, 0x1a, 0x06, 0x0d, 0x00, 0x19, 0x05, 0x0c, 0x02, 0x18, 0x01, 0x04, 0x00, 0x37, 0x0b, 0x17, 0x03, 0x2f,
    0x0a, 0x16, 0x00, 0x2e, 0x09, 0x15, 0x02, 0x2d, 0x01, 0x08, 0x00, 0x36, 0x07, 0x14, 0x03, 0x2c, 0x06, 0x13, 0x00,
    0x2b, 0x05, 0x12, 0x02, 0x2a, 0x01, 0x04, 0x00, 0x35, 0x0b, 0x11, 0x03, 0x29, 0x0a, 0x10, 0x00, 0x28, 0x09, 0x0f,
    0x02, 0x27, 0x01, 0x08, 0x00, 0x34, 0x07, 0x0e, 0x03, 0x26, 0x06, 0x0d, 0x00, 0x25, 0x05, 0x0c, 0x02, 0x24, 0x01,
    0x04, 0x00, 0x33, 0x0b, 0x17, 0x03, 0x23, 0x0a, 0x16, 0x00, 0x22, 0x09, 0x15, 0x02, 0x21, 0x01, 0x08, 0x00, 0x32,
    0x07, 0x14, 0x03, 0x20, 0x06, 0x13, 0x00, 0x1f, 0x05, 0x12, 0x02, 0x1e, 0x01, 0x04, 0x00, 0x31, 0x0b, 0x11, 0x03,
    0x1d, 0x0a, 0x10, 0x00, 0x1c, 0x09, 0x0f, 0x02, 0x1b, 0x01, 0x08, 0x00, 0x30, 0x07, 0x0e, 0x03, 0x1a, 0x06, 0x0d,
    0x00, 0x19, 0x05, 0x0c, 0x02, 0x18, 0x01, 0x04, 0x00};
static const uint8_t yj1_lookup_2[0x10] = {0x08, 0x05, 0x06, 0x04, 0x07, 0x05, 0x06, 0x03,
                                           0x07, 0x05, 0x06, 0x04, 0x07, 0x04, 0x05, 0x03};

static void pal_unpak_model_init(void) {
  int i, ptr;
  for (i = 0; i <= 0x140; i++)
    yj1_leaf[i] = (uint16_t)i;
  for (i = 0; i <= YJ1_ROOT; i++) {
    yj1_value[i] = (uint16_t)i;
    yj1_weight[i] = 1;
    yj1_parent[i] = 0;
    yj1_left[i] = 0;
    yj1_right[i] = 0;
  }
  yj1_parent[YJ1_ROOT] = YJ1_ROOT;
  for (i = 0, ptr = 0x141; ptr <= YJ1_ROOT; i += 2, ptr++) {
    yj1_left[ptr] = (uint16_t)i;
    yj1_right[ptr] = (uint16_t)(i + 1);
    yj1_parent[i] = (uint16_t)ptr;
    yj1_parent[i + 1] = (uint16_t)ptr;
    yj1_weight[ptr] = (uint16_t)(yj1_weight[i] + yj1_weight[i + 1]);
  }
}

static uint16_t pal_unpak_follow(const uint8_t *stream, unsigned int *bitpos) {
  uint16_t node = YJ1_ROOT;
  while (yj1_value[node] > 0x140) {
    uint16_t next = (stream[*bitpos >> 3] >> (*bitpos & 7)) & 1u ? yj1_right[node] : yj1_left[node];
    (*bitpos)++;
    node = next;
  }
  return yj1_value[node];
}

static void pal_rle_copy(uint16_t val) {
  uint16_t node = yj1_leaf[val];
  while (node != YJ1_ROOT) {
    uint16_t other = node;
    while (yj1_weight[other + 1] == yj1_weight[node])
      other++;
    if (other != node) {
      uint16_t w, v;
      if (yj1_value[node] > 0x140) {
        yj1_parent[yj1_left[node]] = other;
        yj1_parent[yj1_right[node]] = other;
      } else {
        yj1_leaf[yj1_value[node]] = other;
      }
      if (yj1_value[other] > 0x140) {
        yj1_parent[yj1_left[other]] = node;
        yj1_parent[yj1_right[other]] = node;
      } else {
        yj1_leaf[yj1_value[other]] = node;
      }
      w = yj1_weight[node];
      v = yj1_value[node];
      yj1_weight[node] = yj1_weight[other];
      yj1_value[node] = yj1_value[other];
      yj1_weight[other] = w;
      yj1_value[other] = v;
      uint16_t l = yj1_left[node], r = yj1_right[node];
      yj1_left[node] = yj1_left[other];
      yj1_right[node] = yj1_right[other];
      yj1_left[other] = l;
      yj1_right[other] = r;
      node = other;
    }
    yj1_weight[node]++;
    node = yj1_parent[node];
  }
  yj1_weight[node]++;
}

static void pal_rle_scan(void) {
  int i;
  for (i = 0; i < 0x141; i++)
    if (yj1_weight[yj1_leaf[i]] & 1)
      pal_rle_copy((uint16_t)i);
  for (i = 0; i <= YJ1_ROOT; i++)
    yj1_weight[i] >>= 1;
}

static void pal_rle_tick(uint16_t val) {
  if (yj1_weight[YJ1_ROOT] == YJ1_MAX_WEIGHT)
    pal_rle_scan();
  pal_rle_copy(val);
}

static int pal_unpak_core(const uint8_t *packed, uint8_t *dst, int dstCap) {
  const uint8_t *stream = packed + 4;
  unsigned int bitpos = 0;
  uint8_t *out = dst;
  uint8_t *outEnd = dst + dstCap;
  int decoded = 0;

  pal_unpak_model_init();

  for (;;) {
    uint16_t val = pal_unpak_follow(stream, &bitpos);
    unsigned int match_start = bitpos;
    pal_rle_tick(val);
    if (val > 0xff) {
      unsigned int idx = 0, temp = 0, dist;
      int i, bits;
      for (i = 0; i < 8; i++)
        idx |= (unsigned int)(stream[(match_start + i) >> 3] >> ((match_start + i) & 7) & 1u) << i;
      bits = yj1_lookup_2[idx & 0xf];
      for (i = 0; i < bits + 6; i++)
        temp |= (unsigned int)(stream[(match_start + i) >> 3] >> ((match_start + i) & 7) & 1u) << i;
      dist = ((unsigned int)yj1_lookup_1[idx & 0xff] << 6) | ((temp >> bits) & 0x3Fu);
      bitpos = match_start + (unsigned int)bits + 6u;
      if (dist == YJ1_EOS)
        break;
      const uint8_t *pre = out - dist - 1;
      int len = (int)val - 0xfd;
      if (len > (int)(outEnd - out))
        len = (int)(outEnd - out);
      for (i = 0; i < len; i++)
        *out++ = *pre++;
      decoded += len;
      if (len < (int)val - 0xfd)
        break;
    } else {
      if (out >= outEnd)
        break;
      *out++ = (uint8_t)val;
      decoded++;
    }
  }
  return decoded;
}

int32_t PAL_Unpak(const void *packed, uint8_t *dst) {
  if (packed == NULL || dst == NULL)
    return 0;
  return pal_unpak_core((const uint8_t *)packed, dst, PAL_UNPAK_DST_CAP);
}

uint16_t PAL_PakSize(uint8_t *buf) {
  uint32_t size;
  if (buf == NULL)
    return 0;
  size = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
  return (uint16_t)(size >> 1);
}

void PAL_ExGop(const uint8_t *map, uint16_t half, uint16_t col, uint16_t row, uint16_t *out2, uint16_t *out1) {
  uint32_t idx =
      (uint32_t)(uint16_t)(((row & 0xFF) << 8) | ((row >> 8) & 0xFF)) + 4u * (uint32_t)col + 2u * (uint32_t)half;
  const uint16_t *cell = (const uint16_t *)(map + (uint16_t)(idx * 2u));
  uint16_t w0 = cell[0];
  uint16_t w1 = cell[1];
  uint16_t v0 = (w0 & 0xFF) | (((w0 & 0x1000) != 0) ? 0x100 : 0);
  uint16_t v1 = (w1 & 0xFF) | (((w1 & 0x1000) != 0) ? 0x100 : 0);
  if (out2)
    *out2 = v0;
  if (out1)
    *out1 = v1;
}
void PAL_Rhrff(uint16_t *x) {
  uint8_t *p;
  int i;
  if (x == NULL)
    return;
  p = (uint8_t *)x + 18;
  for (i = 0; i < 4; i++, p -= 6) {
    memcpy(p + 6, p, 4);
    memcpy(p + 10, p + 4, 2);
  }
}

void PAL_ExGm1(uint16_t x, uint16_t y, const uint8_t *map, uint16_t *out) {
  uint16_t flag = 0, xv = x, yv = y;
  uint32_t byte_index;
  uint16_t w;
  PAL_ExRij(&flag, &xv, &yv);
  byte_index = (uint32_t)yv * 512u + (uint32_t)xv * 8u + (uint32_t)flag * 4u;
  memcpy(&w, map + byte_index, sizeof(w));
  if (out != NULL)
    *out = (w & 0x2000) ? 0x0000 : 0xFFFF;
}

void PAL_ExGm2(uint16_t count, uint16_t skipIdx, uint16_t x, uint16_t y, const uint8_t *events, uint16_t *out) {
  int32_t i;
  const uint16_t *ev = (const uint16_t *)events;
  if (out == NULL || ev == NULL)
    return;
  for (i = 0; i < count; i++, ev += 16) {
    if (i + 1 != skipIdx && (int16_t)ev[6] > 1) {
      int distance = abs((int)(int16_t)ev[1] - (int)(int16_t)x) + 2 * abs((int)(int16_t)ev[2] - (int)(int16_t)y);
      if (distance < 16) {
        *out = 0;
        return;
      }
    }
  }
}

void PAL_ExTF(uint16_t *out, int16_t h, int16_t v) {
  static const uint8_t direction[9] = {1, 2, 2, 1, 0, 3, 0, 0, 3};
  int index;
  if (out == NULL)
    return;
  index = v < 0 ? 0 : (v > 0 ? 6 : 3);
  index += h > 0 ? 2 : (h == 0 ? 1 : 0);
  if (index != 4)
    *out = direction[index];
}
void PAL_ExMyll(uint16_t width) { pal_ntree_width = width; }
void PAL_ExRij(uint16_t *flag, uint16_t *x, uint16_t *y) {
  int16_t xv = (int16_t)*x, yv = (int16_t)*y;
  int16_t v12 = 0, v11 = 0, v10 = 0;
  if (xv < 0 || yv < 0) {
    v12 = 0;
    v11 = 0;
    v10 = 0;
  } else {
    v12 = xv >> 5;
    int16_t v9 = xv & 0x1F;
    v11 = yv >> 4;
    int16_t v8 = yv & 0xF;
    v10 = 0;
    int16_t v3 = v9 + 2 * v8;
    if (v3 >= 16) {
      if (v3 >= 48) {
        ++v12;
        ++v11;
      } else {
        int16_t v4 = 2 * v8 + 32 - v9;
        if (v4 >= 16) {
          if (v4 >= 48)
            ++v11;
          else
            v10 = 1;
        } else
          ++v12;
      }
    }
  }
  if (v12 > 63)
    v12 = 63;
  if (v11 > 127)
    v11 = 127;
  *x = (uint16_t)v12;
  *y = (uint16_t)v11;
  if (flag != NULL)
    *flag = (uint16_t)v10;
}
void PAL_VMap(uint16_t startCol, uint16_t startRow, uint16_t halfFlag, const uint8_t *mapTileData,
              const uint16_t *gopTable, void *target) {
  int curCol = startCol, curRow = startRow;
  int half = halfFlag;
  int screenY = (half != 0) ? -16 : -8;
  int row, h, col;
  uint8_t *dst;
  if (mapTileData == NULL || gopTable == NULL)
    return;
  dst = (target != NULL) ? (uint8_t *)target : (pal_clip_page ? pal_clip_page : screen_surf);

  for (row = 0; row < (int)half + 13; row++, curRow++, screenY += 16) {
    for (h = 0; h < 2; h++) {
      int screenX = (half != 0 ? -32 : -16) + h * 16;
      int cellX = curCol;
      for (col = 0; col < 11; col++, screenX += 32, cellX++) {
        uint32_t cell;
        uint16_t value, frame;
        const uint8_t *sprite;
        if (cellX < 0 || cellX >= 64 || curRow < 0 || curRow >= 128)
          continue;
        memcpy(&cell, mapTileData + 4 * (h + 2 * (cellX + 64 * curRow)), 4);
        value = (uint16_t)(cell & 0x10FF);
        frame = (uint16_t)((value & 0xFF) | ((value >> 4) & 0x100));
        sprite = (const uint8_t *)gopTable + 2 * gopTable[frame];
        pal_blit_rle(dst, sprite, screenX, screenY + h * 8, 0);
        value = (uint16_t)((cell >> 16) & 0x10FF);
        if (value != 0) {
          frame = (uint16_t)((value & 0xFF) | ((value >> 4) & 0x100));
          if (frame > 0) {
            sprite = (const uint8_t *)gopTable + 2 * gopTable[frame - 1];
            pal_blit_rle(dst, sprite, screenX, screenY + h * 8, 0);
          }
        }
      }
    }
  }
}

void PAL_VWindow(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom) {
  pal_vwin_left = left;
  pal_vwin_top = top;
  pal_vwin_right = (right < 320) ? right : 319;
  pal_vwin_bottom = (bottom < 200) ? bottom : 199;
}

void PAL_ExBB(uint16_t thresholdY, uint16_t half, uint16_t col, uint16_t row, const uint8_t *mapData, uint8_t *bitBuf) {
  uint32_t index;
  uint32_t cell;
  uint16_t bottomHeight, topHeight;
  if (bitBuf == NULL || mapData == NULL || row >= 128 || col >= 64 || half > 1)
    return;
  index = (uint32_t)half + 2U * ((uint32_t)col + 64U * (uint32_t)row);
  memcpy(&cell, mapData + 4U * index, sizeof(cell));
  bottomHeight = (uint16_t)(16 * (row + ((cell >> 8) & 0x0F)) + 8 * half + 8);
  topHeight = (uint16_t)(16 * (row + ((cell >> 24) & 0x0F)) + 8 * half + 8);
  if ((cell >> 8) & 0x0F) {
    if ((int16_t)bottomHeight >= (int16_t)thresholdY)
      bitBuf[index] |= 1;
  }
  if ((cell >> 24) & 0x0F) {
    if ((int16_t)topHeight >= (int16_t)thresholdY)
      bitBuf[index] |= 2;
  }
}

void PAL_ExMap(int16_t vpX, int16_t vpY, const uint8_t *mapData, uint8_t *bitBuf, const uint16_t *gopBase) {
  int index;
  const uint16_t *gop = gopBase;
  uint8_t *bitbuf = bitBuf;
  if (gop == NULL || bitbuf == NULL || mapData == NULL)
    return;
  for (index = 0; index < 0x3FFF; index++) {
    uint8_t flags = bitbuf[index];
    if (flags == 0)
      continue;
    int parity = index & 1;
    int screenX = 16 * (parity + 2 * ((index >> 1) & 0x3F)) - vpX - 16;
    int screenY = 8 * (parity + 2 * (index >> 7)) - vpY + 7;
    uint32_t cell;
    memcpy(&cell, mapData + 4 * index, 4);
    if (flags & 1) {
      uint16_t frame = (uint16_t)((cell & 0xFF) | ((cell >> 4) & 0x100));
      PAL_QueueSprite((const uint8_t *)gop + 2 * gop[frame], (int16_t)screenX,
                      (int16_t)(screenY + 8 * ((cell >> 8) & 0xF)), (uint16_t)(8 * ((cell >> 8) & 0xF)),
                      PAL_SpriteHeight((const uint8_t *)gop + 2 * gop[frame]));
    }
    if (flags & 2) {
      uint32_t top = cell >> 16;
      uint16_t frame = (uint16_t)((top & 0xFF) | ((top >> 4) & 0x100));
      if (frame > 0) {
        PAL_QueueSprite((const uint8_t *)gop + 2 * gop[frame - 1], (int16_t)screenX,
                        (int16_t)(screenY + 8 * ((top >> 8) & 0xF) + 1), (uint16_t)(8 * ((top >> 8) & 0xF) + 1),
                        PAL_SpriteHeight((const uint8_t *)gop + 2 * gop[frame - 1]));
      }
    }
  }
}

void PAL_GetBin(uint16_t *out, uint16_t value, uint8_t bit) {
  if (out != NULL)
    *out = (value >> (bit & 31)) & 1;
}

intptr_t PAL_ArrayPtr(void *buf) { return (intptr_t)buf; }

// NOTE: golden 以 16/32 位整数传递文件句柄；remake 主机 FILE* 为 64 位，
// 改用句柄槽表对外发 index+1，避免指针截断。
#define PAL_MAX_FILE_HANDLES 32
static FILE *g_pal_files[PAL_MAX_FILE_HANDLES];

static FILE *pal_file_get(int32_t handle) {
  if (handle <= 0 || handle > PAL_MAX_FILE_HANDLES)
    return NULL;
  return g_pal_files[handle - 1];
}

static int32_t pal_file_put(FILE *fp) {
  int i;
  if (fp == NULL)
    return -1;
  for (i = 0; i < PAL_MAX_FILE_HANDLES; i++) {
    if (g_pal_files[i] == NULL) {
      g_pal_files[i] = fp;
      return i + 1;
    }
  }
  fclose(fp);
  return -1;
}

void PAL_WriteFile(int32_t handle, const void *buf, uint32_t size) {
  FILE *fp = pal_file_get(handle);
  if (fp != NULL)
    fwrite(buf, 1, size, fp);
}
void PAL_ReadFile(int32_t handle, void *buf, uint32_t size) {
  FILE *fp = pal_file_get(handle);
  if (fp != NULL)
    fread(buf, 1, size, fp);
}
void PAL_CloseFile(int32_t handle) {
  FILE *fp = pal_file_get(handle);
  if (fp != NULL) {
    fclose(fp);
    g_pal_files[handle - 1] = NULL;
  }
}
int32_t PAL_GetFileSize(int32_t handle) {
  FILE *fp = pal_file_get(handle);
  if (fp == NULL)
    return 0;
  long old = ftell(fp);
  fseek(fp, 0, SEEK_END);
  long sz = ftell(fp);
  fseek(fp, old, SEEK_SET);
  return (int32_t)sz;
}
void PAL_NTree(int16_t strideFlag, void *listBase) {
  int32_t i, j;
  int32_t n = (int32_t)(pal_tree_count & 0xFF);
  // NOTE: golden ntre 用 ExMyll 残留行界裁剪 putipna；已核查全部流程
  // 该值 ≡ NipW 行数，故 remake 推迟到 NipW 行窗，行为等价。
  (void)strideFlag;
  (void)listBase;
  for (i = 0; i + 1 < n; i++) {
    for (j = i + 1; j < n; j++) {
      if ((int16_t)pal_tree[i].ground_y > (int16_t)pal_tree[j].ground_y) {
        pal_tree_entry_t tmp = pal_tree[i];
        pal_tree[i] = pal_tree[j];
        pal_tree[j] = tmp;
      }
    }
  }
  for (i = 0; i < n; i++) {
    if (pal_splice_n < PAL_SPLICE_MAX && pal_tree[i].bitmap != NULL && pal_tree[i].height != 0) {
      pal_splice[pal_splice_n].bitmap = pal_tree[i].bitmap;
      pal_splice[pal_splice_n].x = pal_tree[i].x;
      pal_splice[pal_splice_n].y = (int)pal_tree[i].ground_y - (int)(int16_t)pal_tree[i].depth - pal_tree[i].height;
      pal_splice[pal_splice_n].isTree = 1;
      pal_splice_n++;
    }
  }
  pal_tree_count = 0;
}
void PAL_AddPic0(uint8_t *dstPage, const uint8_t *srcPage, uint16_t iterations, uint16_t offset) {
  int32_t k;
  if (dstPage == NULL || srcPage == NULL)
    return;
  for (k = 0; k < iterations; k++) {
    uint32_t d = (uint32_t)offset + (uint32_t)k * 6;
    if (d >= 64000)
      // NOTE: remake 增补越界防护，golden 无此检查（安全超集）。
      break;
    dstPage[d] = (uint8_t)((srcPage[d] & 0xF0) | (dstPage[d] & 0x0F));
  }
}
void PAL_AddPic(uint8_t *dstPage, const uint8_t *srcPage, uint16_t iterations, uint16_t offset) {
  int32_t k;
  if (dstPage == NULL || srcPage == NULL)
    return;
  for (k = 0; k < iterations; k++) {
    uint32_t d = (uint32_t)offset + (uint32_t)k * 6;
    int current, target;
    int8_t current_minus_target, target_minus_current;
    if (d >= 64000)
      break;
    current = dstPage[d];
    target = srcPage[d];
    current_minus_target = (int8_t)(uint8_t)(current - target);
    target_minus_current = (int8_t)(uint8_t)(target - current);
    dstPage[d] = (uint8_t)(current + (target_minus_current < 0 ? -1 : 0) - (current_minus_target < 0 ? -1 : 0));
  }
}
static void pal_nip_wave_scroll(uint8_t *dst) {
  int a, b;
  if (!pal_wave_active || dst == NULL)
    return;
  a = pal_wave_ent_a;
  if (a < 0) {
    int n = -a;
    if (n > 320)
      // NOTE: remake 增补越界防护，golden 无此检查（安全超集）。
      n = 320;
    memmove(dst, dst + 320 + a, (size_t)n);
  }
  b = pal_wave_ent_b;
  if (b > 0) {
    if (b > 320)
      b = 320;
    uint8_t *d = dst + 64000 - b;
    const uint8_t *s = dst + 64000 - 320;
    int i;
    for (i = 0; i < b; i++)
      d[i] = s[i];
  }
}

static void pal_nip_copy_rows(uint8_t *dst, int first, int rows, uint8_t effect, int isNipwb) {
  const uint8_t *src = pal_clip_page ? pal_clip_page : screen_surf;
  int r, seg1 = 320 - pal_clip_relx;

  for (r = 0; r < rows; r++) {
    int srccol = (r < pal_clip_n1) ? pal_clip_rely + r : r - pal_clip_n1;
    const uint8_t *srow;
    uint8_t *drow;
    int off = pal_wave_active ? pal_wave_row_off[r] : 0;
    int from;

    if (srccol < 0)
      srccol = 0;
    if (srccol > 199)
      srccol = 199;
    srow = src + srccol * 320;
    drow = dst + (first + r) * 320;

    from = pal_clip_relx + off;
    if (from < 0)
      from = 0;
    if (from > 64000 - seg1)
      from = 64000 - seg1;
    if (isNipwb) {
      int i;
      for (i = 0; i < seg1; i++) {
        uint8_t pixel = srow[from + i];
        uint8_t low = (uint8_t)(effect + (pixel & 0x0F));
        if (low < effect)
          drow[i] = (uint8_t)(pixel & 0xF0);
        else if ((int8_t)low > 15)
          drow[i] = 0x0F;
        else
          drow[i] = (uint8_t)((pixel & 0xF0) | low);
      }
    } else if (from == pal_clip_relx && seg1 == 320) {
      memcpy(drow, srow, 320);
    } else {
      memcpy(drow, srow + from, (size_t)seg1);
    }
    if (pal_clip_relx > 0) {
      from = off;
      if (from < 0)
        from = 0;
      if (from > 64000 - pal_clip_relx)
        from = 64000 - pal_clip_relx;
      if (isNipwb) {
        int i;
        for (i = 0; i < pal_clip_relx; i++) {
          uint8_t pixel = srow[from + i];
          uint8_t low = (uint8_t)(effect + (pixel & 0x0F));
          if (low < effect)
            drow[seg1 + i] = (uint8_t)(pixel & 0xF0);
          else if ((int8_t)low > 15)
            drow[seg1 + i] = 0x0F;
          else
            drow[seg1 + i] = (uint8_t)((pixel & 0xF0) | low);
        }
      } else {
        memcpy(drow + seg1, srow + from, (size_t)pal_clip_relx);
      }
    }
  }
}

static void pal_splice_flush(uint8_t *dstseg, int rowShift, int winTop, int winBot, uint8_t nipwbEffect) {
  int32_t i;
  if (dstseg == NULL) {
    pal_splice_n = 0;
    return;
  }
  for (i = 0; i < pal_splice_n; i++) {
    if (pal_splice[i].bitmap == NULL)
      continue;
    pal_blit_rle_mode(dstseg, pal_splice[i].bitmap, pal_splice[i].x, pal_splice[i].y + rowShift,
                      nipwbEffect != 0 ? 4 : 0, nipwbEffect, 2, winTop, winBot);
  }
  pal_splice_n = 0;
}

void PAL_NipWA(uint16_t startRow, uint16_t rowCount, void *list) {
  uint8_t *dstseg = pal_render_page ? pal_render_page : screen_surf;
  uint8_t *src = pal_clip_page ? pal_clip_page : screen_surf;
  int32_t first = startRow > 200 ? 200 : startRow;
  int32_t rows = rowCount;
  (void)list;

  if (rows > 200 - first)
    rows = 200 - first;
  if (dstseg != src && rows != 0) {
    pal_nip_copy_rows(dstseg, first, rows, 0, 0);
  }
  pal_splice_flush(dstseg, first, first, first + rows, 0);
  pal_nip_wave_scroll(dstseg);
  if (pal_render_page == NULL)
    pal_present_direct();
}
void PAL_NipWB(uint8_t effect, uint16_t rowCount, void *list) {
  uint8_t *src = pal_clip_page ? pal_clip_page : screen_surf;
  uint8_t *dst = pal_render_page ? pal_render_page : screen_surf;
  int32_t rows = rowCount > 200 ? 200 : rowCount;
  (void)list;

  if (dst != src && rows != 0) {
    pal_nip_copy_rows(dst, 0, rows, effect, 1);
  }
  pal_splice_flush(dst, 0, 0, rows, effect);
  pal_nip_wave_scroll(dst);
  if (pal_render_page == NULL)
    pal_present_direct();
}
void PAL_NipWSeg(uint8_t *buf) { pal_render_page = buf; }
void PAL_PutIpNA(int16_t x, int16_t topY, int16_t strideFlag, int16_t rowBound, const void *sprite, intptr_t listBase) {
  // NOTE: golden 以第 4 参为纵窗裁剪（第 3 参选行链 stride）；remake
  // 无行链，裁剪推迟到 NipW 行窗（见 PAL_NTree NOTE），两参仅存照。
  (void)strideFlag;
  (void)rowBound;
  (void)listBase;
  if (sprite == NULL || pal_splice_n >= PAL_SPLICE_MAX)
    return;
  pal_splice[pal_splice_n].bitmap = (const uint8_t *)sprite;
  pal_splice[pal_splice_n].x = x;
  pal_splice[pal_splice_n].y = topY;
  pal_splice[pal_splice_n].isTree = 0;
  pal_splice_n++;
}
void PAL_Rblk(uint16_t left, uint16_t top, uint16_t right, uint16_t bottom, uint8_t color) {
  int32_t x, y;
  if (right >= 320)
    right = 319;
  if (bottom >= 200)
    bottom = 199;
  for (y = top; y <= bottom && y < 200; y++) {
    for (x = left; x <= right && x < 320; x++)
      screen_surf[y * 320 + x] = (uint8_t)(color | (screen_surf[y * 320 + x] & 0x0F));
  }
  pal_present_direct();
}
static int16_t pal_wave_table_at(uint16_t phase) {
  unsigned idx = (phase >> 1) & 31;
  return idx < 16 ? pal_wave_off[idx] : (int16_t)-pal_wave_off[idx - 16];
}

void PAL_RripA(uint16_t rows, uint8_t grade, void *list) {
  int cum = 0, step = 60;
  int32_t count = rows > 200 ? 200 : rows;
  int32_t r, i;
  uint16_t phase = pal_wave_phase;

  (void)list;
  for (i = 0; i < 16; i++) {
    pal_wave_off[i] = (int16_t)((cum * (int)grade) >> 8);
    cum += step;
    step -= 8;
  }
  pal_wave_ent_a = pal_wave_table_at(phase);
  for (r = 0; r < count; r++) {
    int16_t off = pal_wave_table_at(phase);
    pal_wave_row_off[r] = off;
    pal_wave_ent_b = off;
    phase = (uint16_t)((phase + 2) & 0x3E);
  }
  for (r = count; r < 200; r++)
    pal_wave_row_off[r] = 0;
  pal_wave_phase = (uint16_t)((pal_wave_phase + 2) & 0x3E);
  pal_wave_active = true;
}

void PAL_RripAFreeze(void) {
  if (pal_wave_active)
    pal_wave_phase = (uint16_t)((pal_wave_phase - 2) & 0x3E);
}
void PAL_RngPut(const uint16_t *frame) {
  const uint8_t *p = (const uint8_t *)frame;
  uint16_t *dst;
  uint16_t *dstEnd;
  if (frame == NULL)
    return;
  dst = (uint16_t *)screen_surf;
  // NOTE: 目的地上限为 remake 增补防护（golden 无界写，安全超集）。
  dstEnd = dst + 32000;
  for (;;) {
    uint8_t op = *p++;
    if (op == 0 || op >= 0x13) {
      pal_present_direct();
      return;
    }
    switch (op) {
    case 1:
    case 5:
      break;
    case 2:
      dst += 1;
      break;
    case 3:
      dst += *p++ + 1;
      break;
    case 4: {
      uint16_t n;
      memcpy(&n, p, 2);
      p += 2;
      dst += (size_t)n + 1;
      break;
    }
    case 6:
    case 7:
    case 8:
    case 9:
    case 0xA: {
      int count = op - 6 + 1, i;
      if (dst + count > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < count; i++) {
        uint16_t v;
        memcpy(&v, p, 2);
        p += 2;
        *dst++ = v;
      }
      break;
    }
    case 0xB: {
      int count = *p++ + 1, i;
      if (dst + count > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < count; i++) {
        uint16_t v;
        memcpy(&v, p, 2);
        p += 2;
        *dst++ = v;
      }
      break;
    }
    case 0xC: {
      uint16_t n;
      int i;
      memcpy(&n, p, 2);
      p += 2;
      if (dst + n + 1 > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < n + 1; i++) {
        uint16_t v;
        memcpy(&v, p, 2);
        p += 2;
        *dst++ = v;
      }
      break;
    }
    case 0xD:
    case 0xE:
    case 0xF:
    case 0x10: {
      int count = op - 0xD + 2, i;
      uint16_t v;
      memcpy(&v, p, 2);
      p += 2;
      if (dst + count > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < count; i++)
        *dst++ = v;
      break;
    }
    case 0x11: {
      int count = *p++ + 1, i;
      uint16_t v;
      memcpy(&v, p, 2);
      p += 2;
      if (dst + count > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < count; i++)
        *dst++ = v;
      break;
    }
    case 0x12: {
      uint16_t n, v;
      int i;
      memcpy(&n, p, 2);
      p += 2;
      memcpy(&v, p, 2);
      p += 2;
      if (dst + n + 1 > dstEnd) {
        pal_present_direct();
        return;
      }
      for (i = 0; i < n + 1; i++)
        *dst++ = v;
      break;
    }
    }
  }
}
void PAL_PopScreen6(const void *srcBase, uint16_t offset) {
  const uint8_t *buf = (const uint8_t *)srcBase;
  uint32_t i;
  if (buf != NULL) {
    for (i = offset; i < 64000; i += 6)
      screen_surf[i] = buf[i];
  }
  pal_present_direct();
}
void PAL_PopScreen6a(const void *srcBase, uint16_t offset, uint16_t count) {
  const uint8_t *buf = (const uint8_t *)srcBase;
  uint32_t i;
  uint32_t source = offset;
  uint32_t target = offset;

  if (buf != NULL) {
    for (i = 0; i < count && target < 64000 && source < 64000; i++) {
      screen_surf[target] = buf[source];
      source += 6;
      target += 6;
    }
  }
  pal_present_direct();
}
void PAL_PopScreenB(uint16_t *buf, uint16_t effect) {
  uint32_t i;
  uint8_t shift = (uint8_t)effect;

  if (buf == NULL)
    return;
  for (i = 0; i < 64000; i++) {
    uint8_t pixel = buf[i];
    uint8_t low = (uint8_t)(shift + (pixel & 0x0F));
    if (low & 0x80)
      buf[i] = 0;
    else if (low > 15)
      buf[i] = (uint8_t)((pixel & 0xF0) | 15);
    else
      buf[i] = (uint8_t)((pixel & 0xF0) | low);
  }
}

uint16_t PAL_InitCD(void) {
  // NOTE: remake 恒返 0（无光驱，音乐改走 RIX）；golden 失败 → MsgBox 重试或
  // End，该分支在 remake 不可达。
  return 0;
}

void PAL_PlayAvi(void *hwnd, int16_t play, int16_t stop) {
  // NOTE: golden 0x1cf2 经 MCI 播 AVI（释放面 → 切模式 → 播放 → 恢复）；
  // remake 改自持解码（avi.c：RIFF/AVI 解析 + MS Video 1 + PCM u8 音轨，
  // kitty 呈现）。stop!=0 为 golden 不可跳路径，语义照搬；golden 播前
  // 释放 DSound 的等价简化：先停游戏音乐（播完不恢复，golden 同样不恢复）。
  (void)hwnd;
  AUDIO_StopMusic();
  AVI_Play(play, stop == 0);
}

// NOTE: golden setmode 创建 DDraw 并切显示模式；remake 首调自举 kitty 终端
// 后端（成功 0；已建/失败 -1，非 kitty 终端 → stderr 告警 + headless 继续）。
int32_t PAL_SetMode(uint32_t mode) {
  int scale = 2;
  const char *env;
  (void)mode;
  if (kitty_video_up())
    return -1;
  env = getenv("PAL_HEADLESS");
  if (env != NULL && atoi(env) != 0) {
    fprintf(stderr, "[remake] PAL_HEADLESS=1, forcing headless\n");
    return -1;
  }
  env = getenv("PAL_KITTY_SCALE");
  if (env != NULL && *env != 0) {
    int v = atoi(env);
    if (v >= 1 && v <= 4)
      scale = v;
  }
  return kitty_video_init(320, 200, scale) == 0 ? 0 : -1;
}

void PAL_StopApp(int16_t code) {
  if (code == 0)
    PAL_SetTimer();
  else
    PAL_KillTimer();
}
int32_t PAL_InitDSound(uint32_t hwnd) {
  (void)hwnd;
  return AUDIO_Init() == 0 ? 0 : 1;
}
int32_t PAL_InitInput_Win(uint32_t hinst, uint32_t hwnd) {
  // NOTE: golden 0x2114 建 DirectInput 对象；remake 输入直读 kitty 键盘电平
  // 表，无对象可建，恒返 0。
  (void)hinst;
  (void)hwnd;
  return 0;
}
void PAL_ShutdownDSound(void) { AUDIO_Shutdown(); }
void PAL_ShutDownInput(void) {
  // NOTE: golden 释放 DirectInput 对象；remake 无对象可释放，空操作。
}
void PAL_ResetMode(void) {
  // NOTE: golden 释放 DDraw 全套；remake 收回 kitty 终端后端（删图 + 回主屏
  // + 退出 raw + 拆帧槽），进程级终端复原归 PAL_Shutdown/atexit。
  pal_present_direct();
  kitty_video_shutdown();
}

void PAL_LoadDSound(uint16_t soundNum, uint8_t keepFlag) {
  // NOTE: golden 预载 DirectSound 缓冲并按 keepFlag 管理槽位；remake 改由
  // audio.cpp 首播惰性解码并常驻缓存——LoadDSound 为 no-op，keepFlag/Flush 无对应物。
  (void)soundNum;
  (void)keepFlag;
}

void PAL_PlayDSound(uint16_t soundNum, uint8_t keepFlag) {
  (void)keepFlag;
  AUDIO_PlaySound(soundNum);
}
void PAL_FlushDSound(void) {}

// NOTE: 偏离 golden：去掉 Alt（DIK 0x38→取消）、Ctrl（DIK 0x1D→确认）
// 绑定——终端场景 Alt 触发终端菜单、Ctrl+<key> 被终端/退出热键拦截，
// 无有效游戏功能只会误触；其余键位照搬 golden。
static uint8_t pal_keymap[25][2] = {
    {0x01, 0x01}, {0x52, 0x01}, {0x1C, 0x02}, {0x39, 0x02}, {0x9C, 0x02}, {0x48, 0x03}, {0xC8, 0x03},
    {0x50, 0x04}, {0xD0, 0x04}, {0x4B, 0x05}, {0xCB, 0x05}, {0x4D, 0x06}, {0xCD, 0x06}, {0x49, 0x07},
    {0xC9, 0x07}, {0x51, 0x08}, {0xD1, 0x08}, {0x13, 0x09}, {0x1E, 0x0A}, {0x20, 0x0B}, {0x12, 0x0C},
    {0x11, 0x0D}, {0x10, 0x0E}, {0x1F, 0x0F}, {0x21, 0x10},
};

void PAL_ReadKey(uint8_t *keyState) {
  // NOTE: golden 用 DirectInput GetDeviceState 轮询 256B DIK 设备态；remake 由
  // kitty 键盘事件维护同一张电平表（取用前先非阻塞泵 stdin）。
  const uint8_t *down = kitty_key_down_vector();
  int i;
  if (keyState == NULL)
    return;
  for (i = 0; i < 256; i++) {
    uint8_t old = keyState[i];
    keyState[i] = down[i] ? (uint8_t)(old < 2 ? 2 : 3) : (uint8_t)(old < 2 ? 0 : 1);
  }
}

int8_t PAL_CheckKey(uint8_t *keyState) {
  int i;
  int8_t action = 0;
  if (keyState == NULL)
    return 0;
  PAL_ReadKey(keyState);
  for (i = 0; i < 25 && action == 0; i++) {
    if (keyState[pal_keymap[i][0]] == 2)
      action = (int8_t)pal_keymap[i][1];
  }
  return action;
}

uint16_t PAL_GetActiveWindow(void) {
  // NOTE: 有意偏离 golden：恒返 1、不消费焦点电平（golden 失焦分支 = 关红点
  // 收尾）；终端下窗口销毁由 SIGHUP → 泵内 release_resources_exit 承接。
  return 1;
}

uint32_t PAL_GetTicks(void) {
  // NOTE: golden 时基 = timeSetEvent 10ms 计数器；remake 改用 kitty_ticks_ms
  // （CLOCK_MONOTONIC），SetTimer/KillTimer 的计数驻留/续算语义保持。
  return kitty_ticks_ms();
}

void PAL_Shutdown(int16_t code) {
  kitty_terminal_restore();
  exit(code);
}

// NOTE: remake 增补文件定位：golden 按 Win95 CWD 直接打开；remake 增设
// $PAL98_DATA → CWD 目录解析 + 读语义大小写回退（Win95 不敏感 FS 近似）。

static const char *pal_probe_dir(const char *dir, const char *name, char *out, size_t outsz) {
  DIR *dp;
  struct dirent *ent;
  const char *hit = NULL;
  if (dir == NULL || name == NULL || out == NULL)
    return NULL;
  if (snprintf(out, outsz, "%s/%s", dir, name) >= (int)outsz)
    return NULL;
  if (access(out, F_OK) == 0)
    return out;
  dp = opendir(dir);
  if (dp == NULL)
    return NULL;
  while ((ent = readdir(dp)) != NULL) {
    if (strcasecmp(ent->d_name, name) == 0) {
      snprintf(out, outsz, "%s/%s", dir, ent->d_name);
      hit = out;
      break;
    }
  }
  closedir(dp);
  return hit;
}

static const char *pal_resolve_path(const char *name, char *out, size_t outsz) {
  const char *hit = pal_probe_dir(getenv("PAL98_DATA"), name, out, outsz);
  if (hit != NULL)
    return hit;
  return pal_probe_dir(".", name, out, outsz);
}

static FILE *pal_fopen(const char *name, const char *mode) {
  char path[1024];
  const char *data;
  if (name == NULL || mode == NULL)
    return NULL;
  if (mode[0] == 'r') {
    if (pal_resolve_path(name, path, sizeof(path)) == NULL)
      return NULL;
  } else {
    data = getenv("PAL98_DATA");
    if (data != NULL)
      snprintf(path, sizeof(path), "%s/%s", data, name);
    else
      snprintf(path, sizeof(path), "%s", name);
  }
  return fopen(path, mode);
}

// NOTE: golden 用 GDI SimSun 渲染文本（失败 TerminateOnError）；remake 内嵌
// simsun.ttf（.incbin）经 FreeType 单色掩码直写 u8 页，失败降级哑文本。
static bool pal_font_init(void) {
  if (pt_face != NULL || pt_font_failed)
    return pt_face != NULL;
  if (FT_Init_FreeType(&pt_library) != 0) {
    pt_font_failed = true;
    fprintf(stderr, "[remake] FreeType init failed, text disabled\n");
    return false;
  }
  if (FT_New_Memory_Face(pt_library, pal_simsun_ttf_start, (FT_Long)(pal_simsun_ttf_end - pal_simsun_ttf_start), 0,
                         &pt_face) != 0) {
    FT_Done_FreeType(pt_library);
    pt_library = NULL;
    pt_font_failed = true;
    fprintf(stderr, "[remake] embedded simsun.ttf bad, text disabled\n");
    return false;
  }
  FT_Set_Pixel_Sizes(pt_face, PT_FONT_HEIGHT, PT_FONT_HEIGHT);
  FT_Select_Charmap(pt_face, FT_ENCODING_UNICODE);
  return true;
}

static int pal_mb2wc(const char *mbs, int mbsLength, wchar_t *wcs, int wcsLength) {
  char *in_ptr, *out_ptr;
  size_t in_bytes, out_bytes_max, out_bytes_left;
  if (mbs == NULL || wcs == NULL || wcsLength <= 0)
    return 0;
  if (pt_iconv == (iconv_t)-1) {
    if (!pt_iconv_failed) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      pt_iconv = iconv_open("UCS-4-INTERNAL", "GBK");
#else
      pt_iconv = iconv_open("UTF-32LE", "GBK");
#endif
    }
    if (pt_iconv == (iconv_t)-1) {
      pt_iconv_failed = true;
      fprintf(stderr, "[remake] iconv GBK unavailable, text disabled\n");
      return 0;
    }
  }
  in_ptr = (char *)mbs;
  out_ptr = (char *)wcs;
  out_bytes_max = (size_t)wcsLength * sizeof(wchar_t);
  out_bytes_left = out_bytes_max;
  in_bytes = (mbsLength == -1) ? strlen(mbs) + 1 : (size_t)mbsLength;
  for (; in_bytes != 0; in_bytes--) {
    if (iconv(pt_iconv, &in_ptr, &in_bytes, &out_ptr, &out_bytes_left) != (size_t)-1)
      break;
  }
  return (int)((out_bytes_max - out_bytes_left) / sizeof(wchar_t));
}

static int pal_char_width(uint32_t wch) {
  if (pt_face != NULL && FT_Load_Char(pt_face, wch, FT_LOAD_NO_BITMAP) == 0)
    return (int)(pt_face->glyph->advance.x >> 6);
  return (wch < 0x80) ? 8 : 16;
}

static void pal_text_char(uint32_t wch, uint8_t *page, int x, int y, uint8_t color) {
  FT_GlyphSlot slot;
  FT_Bitmap *bm;
  int ascender, start_y, start_x, r, c;
  if (pt_face == NULL)
    return;
  if (FT_Load_Char(pt_face, wch, FT_LOAD_RENDER | FT_LOAD_MONOCHROME) != 0)
    return;
  slot = pt_face->glyph;
  bm = &slot->bitmap;
  ascender = (int)(pt_face->size->metrics.ascender >> 6);
  start_y = y + ascender - slot->bitmap_top;
  start_x = x + slot->bitmap_left;
  for (r = 0; r < (int)bm->rows; r++) {
    const uint8_t *src_row = bm->buffer + (size_t)r * (size_t)bm->pitch;
    uint8_t *dst_row;
    int ty = start_y + r;
    if (ty < 0 || ty >= 200)
      continue;
    dst_row = page + ty * 320;
    for (c = 0; c < (int)bm->width; c++) {
      int tx = start_x + c;
      if (tx < 0 || tx >= 320)
        continue;
      if (src_row[c >> 3] & (0x80 >> (c & 7)))
        dst_row[tx] = color;
    }
  }
}

static void pal_text_draw(LPCWSTR text, PAL_POS pos, uint8_t color, bool shadow, uint8_t *page) {
  int x = pos.x, y = pos.y;
  if (page == NULL || text == NULL)
    return;
  if (!pal_font_init())
    return;
  if (shadow) {
    int sx = x;
    const wchar_t *p = text;
    while (*p) {
      pal_text_char((uint32_t)*p, page, sx + 1, y + 1, 0);
      sx += pal_char_width((uint32_t)*p);
      p++;
    }
  }
  while (*text) {
    pal_text_char((uint32_t)*text, page, x, y, color);
    x += pal_char_width((uint32_t)*text);
    text++;
  }
}

void PAL_DrawString(const char *text, int16_t x, int16_t y, int16_t shadowFlag, uint8_t color, void *targetPage) {
  wchar_t wide[1024];
  int length;
  uint8_t drawColor = color;
  uint8_t *page = (targetPage == 0) ? screen_surf : (uint8_t *)targetPage;

  if (text == NULL || page == NULL)
    return;
  length = pal_mb2wc(text, -1, wide, (int)(sizeof(wide) / sizeof(wide[0])));
  if (length <= 0)
    return;
  if (length < (int)(sizeof(wide) / sizeof(wide[0])))
    wide[length] = 0;
  wide[(sizeof(wide) / sizeof(wide[0])) - 1] = 0;
  pal_text_draw(wide, PAL_XY(x, y), drawColor, (shadowFlag == 0), page);
  if (targetPage == 0)
    pal_screen_dirty = 1;
}

// NOTE: remake 增补 VB Rnd 同构 LCG（golden Rnd 由 VB 运行时提供）；
// rtcRandomize 在 remake 为 no-op，种子取地址熵。
static uint32_t rnd_seed = 0x2A5F1E3u;

float RandomFloat(float from, float to) {
  rnd_seed = (rnd_seed * 0x343FDu + 0x269EC3u) & 0xFFFFFFu;
  if (rnd_seed == 0)
    rnd_seed = 0x269EC3u;
  return from + (to - from) * ((float)rnd_seed / 16777216.0f);
}

double VB_rtcRandomNext(void) { return RandomFloat(0.0f, 1.0f); }

int VB_rtcAnsiValueBstr(const char *str) {
  if (!str || !str[0])
    return 0;
  return (unsigned char)str[0];
}

void VB_Mid(char *dst, const char *src, int start, int len) {
  strncpy(dst, src + start - 1, len);
  dst[len] = '\0';
}

int VB_rtcMsgBox(const char *title, int flags, const char *msg) { return 1; }

void VB_Left(char *dst, const char *src, int len) {
  strncpy(dst, src, len);
  dst[len] = '\0';
}

int VB_Len(const char *str) { return (int)strlen(str); }
int VB_Abs(int v) { return v < 0 ? -v : v; }
int VB_Int(double v) { return (int)floor(v); }
double VB_CSng(int v) { return (float)v; }

// NOTE: remake ABI 增设 dstSize 上限参（golden 两参、无上限）；核心与 golden 一致。
int32_t PAL_Decompress(uint8_t *src, uint8_t *dst, int32_t dstSize) {
  if (src == NULL || dst == NULL || dstSize <= 0)
    return 0;
  return pal_unpak_core(src, dst, dstSize);
}

int32_t pal_lcreat(const char *filename, int attr) {
  (void)attr;
  FILE *fp = pal_fopen(filename, "wb");
  return pal_file_put(fp);
}

int32_t pal_lopen(const char *filename, int flags) {
  (void)flags;
  FILE *fp = pal_fopen(filename, "rb");
  return pal_file_put(fp);
}

int32_t pal_hread(int32_t handle, void *buf, int32_t count) {
  FILE *fp = pal_file_get(handle);
  if (fp == NULL || buf == NULL)
    return 0;
  return (int32_t)fread(buf, 1, (size_t)count, fp);
}

void pal_lclose(int32_t handle) {
  FILE *fp = pal_file_get(handle);
  if (fp != NULL) {
    fclose(fp);
    g_pal_files[handle - 1] = NULL;
  }
}

int32_t pal_llseek(int32_t handle, int32_t offset, int32_t whence) {
  FILE *fp = pal_file_get(handle);
  if (fp == NULL)
    return -1;
  if (fseek(fp, offset, whence) != 0)
    return -1;
  return (int32_t)ftell(fp);
}

int32_t pal_mciSendStringA(const char *cmd, char *retbuf, int32_t retlen) {
  // NOTE: golden 经 MCI 播 MIDI（MUSICS\NNN.MID）/CD-DA；remake 无 MCI 与光驱：
  // stop/close 转发 AUDIO_StopMusic，"play midi" 重发 RIX，cdtrack 恒 no-op。
  if (cmd == NULL)
    return 0;
  if (strncmp(cmd, "stop", 4) == 0 || strncmp(cmd, "close", 5) == 0) {
    AUDIO_StopMusic();
    return 0;
  }
  if (strncmp(cmd, "status midi mode", 16) == 0) {
    int playing = AUDIO_MusicPlaying();
    if (retbuf != NULL && retlen > 8) {
      snprintf(retbuf, 8, "%s", playing ? "playing" : "stopped");
    }
    return 0;
  }
  if (strcmp(cmd, "play midi") == 0) {
    if (midi_active == 1 && AUDIO_MusicPlaying() == 0)
      AUDIO_PlayMusic(music_track_arg, midi_playing ? TRUE : FALSE, 0);
    return 0;
  }
  (void)retbuf;
  (void)retlen;
  return 0;
}

void pal_playMidi(uint16_t musicNum, uint16_t loopFlag) { AUDIO_PlayMusic(musicNum, loopFlag ? TRUE : FALSE, 0); }

// NOTE: golden CD-DA 经 MCI 播放（remake 无光驱，恒不可用）；回退改放 RIX
// musicNum（SDLPAL 0x00A3 约定，与 golden 非 CD 分支同参）。
void pal_playCdTrack(uint16_t trackNum, uint16_t musicNum, uint16_t trackValid) {
  (void)trackNum;
  AUDIO_PlayMusic(musicNum, trackValid ? TRUE : FALSE, 0);
}

// NOTE: golden 窗体 Timer 由 VB 运行时泵送；remake 由事件泵/忙等点按 10ms
// 粒度手动调度 timer_midi_cd_callback。
static void pal_timer_dispatch(void) {
  static uint32_t last_dispatch;
  uint32_t now = kitty_ticks_ms();
  if (now - last_dispatch < 10)
    return;
  last_dispatch = now;
  timer_midi_cd_callback();
}

// NOTE: remake 增补事件泵：非阻塞排空 kitty 键盘事件并消费退出请求
// （SIGINT/SIGTERM/SIGHUP → release_resources_exit），替代 golden 消息泵。
static void pal_events_pump(void) {
  kitty_pump_input();
  if (kitty_quit_requested())
    release_resources_exit();
  pal_timer_dispatch();
  if (pal_screen_dirty)
    pal_present_direct();
}

void pal_rtcDoEvents(void) { pal_events_pump(); }

static FILE *g_worddat;
static int32_t g_worddat_handle;

// NOTE: golden VB Open 缺失文件即 error 53 终止，此处同语义。
void pal_vbOpenWordDat(void) {
  FILE *fp = pal_fopen("WORD.DAT", "rb");
  if (fp == NULL) {
    fprintf(stderr, "[remake] 无法打开资源文件 WORD.DAT（检查 $PAL98_DATA 或 CWD 数据目录）\n");
    exit(1);
  }
  g_worddat = fp;
  g_worddat_handle = (int32_t)(intptr_t)fp;
}

int32_t pal_vbReadWordDat(uint8_t *dst) {
  if (g_worddat == NULL)
    return 0;
  return (int32_t)fread(dst, 1, 10, g_worddat);
}

void pal_vbCloseWordDat(void) {
  if (g_worddat != NULL) {
    fclose(g_worddat);
    g_worddat = NULL;
  }
  g_worddat_handle = -1;
}
