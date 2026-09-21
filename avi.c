// NOTE: remake 增补的 AVI 播放，golden 无此实现——PAL_PlayAvi（pal_dll.c
// 0x1cf2）经 MCI avivideo 播放并交系统解码/混音，remake 自持：RIFF/AVI 解析 +
// MS Video 1 解码（等效移植 FFmpeg msvideo1_decode_16bit）+ PCM u8 独立设备 +
// RGB555 真彩直呈；跳过/尾音/中断语义照搬 golden。安全偏差：音频时钟 = 回调
// 喂帧计数（golden 由 MCI 内部同步）；放大最近邻（SDLPAL 为 LINEAR）；视频
// 收窄 MSVC 16bpp、音频收窄 PCM u8（golden 交 MCI 任意解码器）。

#include "avi.h"

#include "kitty.h"
#include "pal.h"

#include "miniaudio/miniaudio.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t off, size;
} avi_chunk_ref_t;

typedef struct {
  int nframes;
  avi_chunk_ref_t *frames;
  int v_w, v_h;
  int64_t v_scale, v_rate;
  int has_audio;
  int a_ch;
  int64_t a_rate;
  uint8_t *pcm;
  int64_t pcm_bytes;
} avi_t;

typedef struct {
  avi_chunk_ref_t *frames;
  int nframes, frames_cap;
  uint8_t *pcm;
  int64_t pcm_bytes, pcm_cap;
} avi_scan_t;

static uint32_t avi_rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t avi_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

#define AVI_FCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

// NOTE: golden 在 CWD 找不到 N.avi 时回退游戏光盘；remake 无光盘，始终在
// 数据目录（$PAL98_DATA，未设即 CWD）下找。
static FILE *avi_open_data(const char *name) {
  char path[1024];
  const char *dir = getenv("PAL98_DATA");
  if (snprintf(path, sizeof path, dir != NULL ? "%s/%s" : "%s", dir != NULL ? dir : name, dir != NULL ? name : "") >=
      (int)sizeof path)
    return NULL;
  return fopen(path, "rb");
}

static uint8_t *avi_slurp(FILE *fp, long *out_size) {
  long fsz;
  uint8_t *buf;
  fseek(fp, 0, SEEK_END);
  fsz = ftell(fp);
  rewind(fp);
  if (fsz <= 0 || (unsigned long)fsz > 0x40000000ul)
    return NULL;
  buf = (uint8_t *)malloc((size_t)fsz);
  if (buf == NULL || fread(buf, 1, (size_t)fsz, fp) != (size_t)fsz) {
    free(buf);
    return NULL;
  }
  *out_size = fsz;
  return buf;
}

static void avi_parse_strl(const uint8_t *d, size_t body, size_t end, avi_t *av, int stream_index, int *vstream,
                           int *astream) {
  const uint8_t *strh = NULL, *strf = NULL;
  uint32_t strf_sz = 0, type, handler;
  size_t off = body;

  while (off + 8 <= end) {
    uint32_t fcc = avi_rd32(d + off), sz = avi_rd32(d + off + 4);
    size_t cb = off + 8;
    if (sz > end - cb)
      sz = (uint32_t)(end - cb);
    if (fcc == AVI_FCC('s', 't', 'r', 'h') && sz >= 0x28 && strh == NULL) {
      strh = d + cb;
    } else if (fcc == AVI_FCC('s', 't', 'r', 'f') && strf == NULL) {
      strf = d + cb;
      strf_sz = sz;
    }
    off = cb + sz + (sz & 1);
  }
  if (strh == NULL)
    return;
  type = avi_rd32(strh);
  handler = avi_rd32(strh + 4);
  if (type == AVI_FCC('v', 'i', 'd', 's') && *vstream < 0) {
    // NOTE: 视频收窄 MS Video 1 16bpp（安全偏差，见文件头）。
    if (strf != NULL && strf_sz >= 40 && avi_rd16(strf + 14) == 16 &&
        ((handler & ~0x20202020u) == AVI_FCC('C', 'R', 'A', 'M') ||
         (handler & ~0x20202020u) == AVI_FCC('M', 'S', 'V', 'C'))) {
      *vstream = stream_index;
      av->v_w = (int)avi_rd32(strf + 4);
      av->v_h = (int)avi_rd32(strf + 8);
      av->v_scale = avi_rd32(strh + 20);
      av->v_rate = avi_rd32(strh + 24);
    } else {
      fprintf(stderr, "[avi] stream %d: unsupported video, skipped\n", stream_index);
    }
  } else if (type == AVI_FCC('a', 'u', 'd', 's') && *astream < 0) {
    // NOTE: 音频收窄 PCM u8（安全偏差，见文件头），其余静音降级。
    *astream = stream_index;
    if (strf != NULL && strf_sz >= 16 && avi_rd16(strf) == 1 && avi_rd16(strf + 14) == 8) {
      av->a_ch = avi_rd16(strf + 2);
      av->a_rate = avi_rd32(strf + 4);
      av->has_audio = (av->a_ch == 1 || av->a_ch == 2) && av->a_rate > 0;
    }
    if (!av->has_audio)
      fprintf(stderr, "[avi] stream %d: unsupported audio, playing silent\n", stream_index);
  }
}

// movi 块扫描：视频帧块入 frames（'dc'/'db'），音频块顺序拼接入 pcm，
// 'rec ' 子 LIST 递归下钻。
static int avi_scan_chunks(const uint8_t *d, size_t body, size_t end, const avi_t *av, int vstream, int astream,
                           avi_scan_t *sc, int depth) {
  size_t off = body;
  while (off + 8 <= end) {
    uint32_t sz = avi_rd32(d + off + 4);
    size_t cb = off + 8;
    const uint8_t *id = d + off;
    if (sz > end - cb)
      sz = (uint32_t)(end - cb);
    if (id[0] == 'L' && id[1] == 'I' && id[2] == 'S' && id[3] == 'T' && sz >= 4 &&
        avi_rd32(d + cb) == AVI_FCC('r', 'e', 'c', ' ')) {
      // NOTE: rec 递归深度上限为 remake 增补防御（正常数据仅一层）。
      if (depth >= 4) {
        fprintf(stderr, "[avi] rec nesting too deep, refused\n");
        return -1;
      }
      if (avi_scan_chunks(d, cb + 4, cb + sz, av, vstream, astream, sc, depth + 1) != 0)
        return -1;
    } else if (id[0] >= '0' && id[0] <= '9' && id[1] >= '0' && id[1] <= '9') {
      int s = (id[0] - '0') * 10 + (id[1] - '0');
      if (s == vstream && id[2] == 'd' && (id[3] == 'c' || id[3] == 'b')) {
        if (sc->nframes == sc->frames_cap) {
          avi_chunk_ref_t *nf;
          sc->frames_cap = sc->frames_cap ? sc->frames_cap * 2 : 256;
          nf = (avi_chunk_ref_t *)realloc(sc->frames, (size_t)sc->frames_cap * sizeof *nf);
          if (nf == NULL)
            goto oom;
          sc->frames = nf;
        }
        sc->frames[sc->nframes].off = (uint32_t)cb;
        sc->frames[sc->nframes].size = sz;
        sc->nframes++;
      } else if (s == astream && id[2] == 'w' && id[3] == 'b' && av->has_audio) {
        if (sc->pcm_bytes + sz > sc->pcm_cap) {
          int64_t nc = sc->pcm_cap ? sc->pcm_cap * 2 : 65536;
          uint8_t *np;
          while (nc < sc->pcm_bytes + sz)
            nc *= 2;
          np = (uint8_t *)realloc(sc->pcm, (size_t)nc);
          if (np == NULL)
            goto oom;
          sc->pcm = np;
          sc->pcm_cap = nc;
        }
        memcpy(sc->pcm + sc->pcm_bytes, d + cb, sz);
        sc->pcm_bytes += sz;
      }
    }
    off = cb + sz + (sz & 1);
  }
  return 0;
oom:
  fprintf(stderr, "[avi] out of memory scanning movi\n");
  return -1;
}

static int avi_parse(const uint8_t *d, size_t size, avi_t *av) {
  size_t off;
  int vstream = -1, astream = -1, nstreams = 0;

  memset(av, 0, sizeof *av);
  if (size < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "AVI ", 4) != 0)
    return -1;

  off = 12;
  while (off + 8 <= size) {
    uint32_t fcc = avi_rd32(d + off);
    uint32_t sz = avi_rd32(d + off + 4);
    size_t body = off + 8;
    if (sz > size - body)
      sz = (uint32_t)(size - body);
    if (fcc == AVI_FCC('L', 'I', 'S', 'T') && sz >= 4) {
      uint32_t lt = avi_rd32(d + body);
      size_t sub = body + 4, send = body + sz;
      if (lt == AVI_FCC('h', 'd', 'r', 'l')) {
        while (sub + 8 <= send) {
          uint32_t cf = avi_rd32(d + sub);
          uint32_t cs = avi_rd32(d + sub + 4);
          size_t cb = sub + 8;
          if (cs > send - cb)
            cs = (uint32_t)(send - cb);
          if (cf == AVI_FCC('L', 'I', 'S', 'T') && cs >= 4 && avi_rd32(d + cb) == AVI_FCC('s', 't', 'r', 'l')) {
            avi_parse_strl(d, cb + 4, cb + cs, av, nstreams, &vstream, &astream);
            nstreams++;
          }
          sub = cb + cs + (cs & 1);
        }
      } else if (lt == AVI_FCC('m', 'o', 'v', 'i')) {
        avi_scan_t sc = {0};
        if (vstream < 0) {
          fprintf(stderr, "[avi] no supported video stream before movi, refused\n");
          return -1;
        }
        if (avi_scan_chunks(d, sub, send, av, vstream, astream, &sc, 0) != 0) {
          free(sc.frames);
          free(sc.pcm);
          return -1;
        }
        av->frames = sc.frames;
        av->nframes = sc.nframes;
        av->pcm = sc.pcm;
        av->pcm_bytes = sc.pcm_bytes;
      }
    }
    off = body + sz + (sz & 1);
  }

  if (vstream < 0 || av->nframes <= 0 || av->v_rate <= 0 || av->v_scale <= 0 || av->v_w <= 0 || av->v_h <= 0 ||
      av->v_w % 4 != 0 || av->v_h % 4 != 0 || av->v_w > 320 || av->v_h > 200) {
    fprintf(stderr, "[avi] unsupported stream/geometry %dx%d\n", av->v_w, av->v_h);
    return -1;
  }
  return 0;
}

// NOTE: golden 交 MCI/系统解码器；remake 等效移植 FFmpeg msvideo1.c 的
// msvideo1_decode_16bit。skip 块保留上一帧像素，frame 缓冲须跨帧复用。
static void avi_cram16(const uint8_t *buf, int size, uint16_t *pix, int w, int h) {
  int blocks_wide = w / 4, blocks_high = h / 4;
  int stride = w;
  int row_dec = stride + 4;
  int total_blocks = blocks_wide * blocks_high;
  int stream_ptr = 0, skip_blocks = 0;
  int block_y, block_x, i, block_ptr;
  uint16_t colors[8];

  for (block_y = blocks_high; block_y > 0; block_y--) {
    block_ptr = ((block_y * 4) - 1) * stride;
    for (block_x = blocks_wide; block_x > 0; block_x--) {
      int pixel_ptr;
      uint8_t byte_a, byte_b;
      if (skip_blocks) {
        block_ptr += 4;
        skip_blocks--;
        total_blocks--;
        continue;
      }
      pixel_ptr = block_ptr;
      if (stream_ptr + 2 > size)
        return;
      byte_a = buf[stream_ptr++];
      byte_b = buf[stream_ptr++];
      if (byte_a == 0 && byte_b == 0 && total_blocks == 0)
        return;
      if ((byte_b & 0xFC) == 0x84) {
        skip_blocks = ((byte_b - 0x84) << 8) + byte_a - 1;
      } else if (byte_b < 0x80) {
        uint16_t flags = (uint16_t)((byte_b << 8) | byte_a);
        int py, px;
        if (stream_ptr + 4 > size)
          return;
        colors[0] = (uint16_t)(buf[stream_ptr] | (buf[stream_ptr + 1] << 8));
        stream_ptr += 2;
        colors[1] = (uint16_t)(buf[stream_ptr] | (buf[stream_ptr + 1] << 8));
        stream_ptr += 2;
        if (colors[0] & 0x8000) {
          if (stream_ptr + 12 > size)
            return;
          for (i = 0; i < 6; i++) {
            colors[2 + i] = (uint16_t)(buf[stream_ptr] | (buf[stream_ptr + 1] << 8));
            stream_ptr += 2;
          }
          for (py = 0; py < 4; py++) {
            for (px = 0; px < 4; px++, flags >>= 1)
              pix[pixel_ptr++] = colors[((py & 0x2) << 1) + (px & 0x2) + ((flags & 0x1) ^ 1)];
            pixel_ptr -= row_dec;
          }
        } else {
          for (py = 0; py < 4; py++) {
            for (px = 0; px < 4; px++, flags >>= 1)
              pix[pixel_ptr++] = colors[(flags & 0x1) ^ 1];
            pixel_ptr -= row_dec;
          }
        }
      } else {
        uint16_t c = (uint16_t)((byte_b << 8) | byte_a);
        int py, px;
        for (py = 0; py < 4; py++) {
          for (px = 0; px < 4; px++)
            pix[pixel_ptr++] = c;
          pixel_ptr -= row_dec;
        }
      }
      block_ptr += 4;
      total_blocks--;
    }
  }
}

static ma_device g_avi_dev;
static bool g_avi_dev_ok;
static _Atomic int64_t g_avi_cursor;
static const uint8_t *g_avi_pcm;
static int64_t g_avi_pcm_frames;
static int g_avi_ch;

static void avi_audio_cb(ma_device *dev, void *out, const void *in, ma_uint32 frames) {
  uint8_t *dst = (uint8_t *)out;
  int64_t pos = atomic_load(&g_avi_cursor);
  int64_t remain = g_avi_pcm_frames - pos;
  int64_t n = (frames < remain) ? (int64_t)frames : remain;
  (void)dev;
  (void)in;
  if (n > 0)
    memcpy(dst, g_avi_pcm + pos * g_avi_ch, (size_t)n * (size_t)g_avi_ch);
  if (n < (int64_t)frames)
    memset(dst + n * g_avi_ch, 0x80, (size_t)(frames - n) * (size_t)g_avi_ch);
  atomic_store(&g_avi_cursor, pos + n);
}

// NOTE: A/V 时钟偏离 golden（MCI 内部同步）：音频回调喂帧计数作主时钟；
// 音轨耗尽或无声时回退墙钟，耗尽锚定音轨终点（防短音轨卡死帧循环）。
static int64_t avi_now_us(const avi_t *av, uint32_t start_ms, int64_t *drain_anchor) {
  if (av->has_audio && g_avi_dev_ok) {
    int64_t pos = atomic_load(&g_avi_cursor);
    int64_t end = g_avi_pcm_frames;
    if (pos < end)
      return pos * 1000000 / av->a_rate;
    if (*drain_anchor < 0)
      *drain_anchor = (int64_t)(kitty_ticks_ms() - start_ms) * 1000;
    return end * 1000000 / av->a_rate + ((int64_t)(kitty_ticks_ms() - start_ms) * 1000 - *drain_anchor);
  }
  return (int64_t)(kitty_ticks_ms() - start_ms) * 1000;
}

// NOTE: 跳过/中断语义照搬 golden：~1s 后才武装跳过（g_wave_active 0→1）；
// 不可跳路径也取键向量吃掉 latch（golden 子类 wndproc 返回 0 不透传）；
// SIGINT/SIGTERM 恒可退（golden 泵收 WM_CLOSE 等价）。
static int avi_poll(int skippable, uint32_t start_ms) {
  const uint8_t *kd;
  int i;
  if (kitty_quit_requested())
    return 1;
  kd = kitty_key_down_vector();
  if (!skippable)
    return 0;
  if (kitty_ticks_ms() - start_ms < 1000)
    return 0;
  for (i = 0; i < 256; i++)
    if (kd[i])
      return 1;
  return 0;
}

int AVI_Play(int number, int skippable) {
  char name[16];
  long fsz;
  uint8_t *filebuf;
  uint16_t *frame;
  avi_t av;
  uint32_t start_ms;
  int64_t drain_anchor = -1, end_us;
  int i, aborted = 0;

  // NOTE: golden SetDisplayMode 级联全失败 → 不播直接返回（remake：kitty 未起）。
  if (!kitty_video_up())
    return -1;
  snprintf(name, sizeof name, "%d.AVI", number);
  filebuf = NULL;
  fsz = 0;
  {
    FILE *fp = avi_open_data(name);
    if (fp == NULL) {
      fprintf(stderr, "[avi] %s not found, skipped\n", name);
      return -1;
    }
    filebuf = avi_slurp(fp, &fsz);
    fclose(fp);
    if (filebuf == NULL)
      return -1;
  }

  if (avi_parse(filebuf, (size_t)fsz, &av) != 0) {
    fprintf(stderr, "[avi] %s: parse failed or unsupported, skipped\n", name);
    free(filebuf);
    free(av.frames);
    free(av.pcm);
    return -1;
  }
  frame = (uint16_t *)calloc((size_t)av.v_w * av.v_h, 2);
  if (frame == NULL) {
    free(filebuf);
    free(av.frames);
    free(av.pcm);
    return -1;
  }

  // NOTE: 偏离 golden（切显示模式即黑屏）：AVI 不经 screen_surf 呈现，但清
  // 黑游戏画布——结局播完后的退出路径会现屏一次，黑底避免闪现旧画面。
  memset(screen_surf, 0, 320 * 200);

  // NOTE: golden 播前释放 DirectSound、播后重建；remake 简化为调用方先停
  // 游戏音乐（PAL_PlayAvi 桥），本音轨设备与 audio.cpp 设备并存。
  g_avi_pcm = av.pcm;
  g_avi_pcm_frames = av.has_audio ? av.pcm_bytes / av.a_ch : 0;
  g_avi_ch = av.a_ch;
  atomic_store(&g_avi_cursor, 0);
  if (av.has_audio) {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_u8;
    cfg.playback.channels = (ma_uint32)av.a_ch;
    cfg.sampleRate = (ma_uint32)av.a_rate;
    cfg.dataCallback = avi_audio_cb;
    if (ma_device_init(NULL, &cfg, &g_avi_dev) == MA_SUCCESS) {
      if (ma_device_start(&g_avi_dev) == MA_SUCCESS)
        g_avi_dev_ok = true;
      else
        ma_device_uninit(&g_avi_dev);
    }
    if (!g_avi_dev_ok) {
      fprintf(stderr, "[avi] audio device unavailable, playing silent\n");
      av.has_audio = 0;
    }
  }

  start_ms = kitty_ticks_ms();
  for (i = 0; i < av.nframes; i++) {
    int64_t due_us = (int64_t)i * av.v_scale * 1000000 / av.v_rate;
    while (avi_now_us(&av, start_ms, &drain_anchor) < due_us) {
      if (avi_poll(skippable, start_ms)) {
        aborted = 1;
        break;
      }
      kitty_delay_ms(5);
    }
    if (aborted)
      break;
    avi_cram16(filebuf + av.frames[i].off, (int)av.frames[i].size, frame, av.v_w, av.v_h);
    kitty_present_frame_tc(frame, av.v_w, av.v_h);
  }
  // NOTE: 尾音收尾照搬 golden（轮询 status avi mode 到非 playing 才返回）：
  // 等到 max(视频时长, 音轨时长)。
  end_us = (int64_t)av.nframes * av.v_scale * 1000000 / av.v_rate;
  if (av.has_audio && g_avi_dev_ok && g_avi_pcm_frames * 1000000 / av.a_rate > end_us)
    end_us = g_avi_pcm_frames * 1000000 / av.a_rate;
  while (!aborted && !avi_poll(skippable, start_ms) && avi_now_us(&av, start_ms, &drain_anchor) < end_us)
    kitty_delay_ms(5);

  if (g_avi_dev_ok) {
    ma_device_uninit(&g_avi_dev);
    g_avi_dev_ok = false;
  }
  free(frame);
  free(av.frames);
  free(av.pcm);
  free(filebuf);
  return 0;
}
