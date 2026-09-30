// NOTE: remake 增补的 kitty graphics/keyboard protocol 终端后端，
// golden 无对应（原版呈现/输入 = GDI/DirectDraw/DirectInput）。

#define _GNU_SOURCE
#include "kitty.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void k_write_all(const char *s, size_t n) {
  size_t off = 0;
  while (off < n) {
    ssize_t w = write(STDOUT_FILENO, s + off, n - off);
    if (w < 0) {
      if (errno == EINTR)
        continue;
      return;
    }
    off += (size_t)w;
  }
}

static void k_put(const char *s) { k_write_all(s, strlen(s)); }

static void k_b64(const uint8_t *src, size_t len, char *out) {
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0, i;
  for (i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)src[i] << 16;
    if (i + 1 < len)
      n |= (uint32_t)src[i + 1] << 8;
    if (i + 2 < len)
      n |= (uint32_t)src[i + 2];
    out[o++] = T[(n >> 18) & 63];
    out[o++] = T[(n >> 12) & 63];
    out[o++] = (i + 1 < len) ? T[(n >> 6) & 63] : '=';
    out[o++] = (i + 2 < len) ? T[n & 63] : '=';
  }
  out[o] = 0;
}

static struct termios k_saved_tio;
static bool k_raw = false, k_alt = false;
static volatile sig_atomic_t k_quit = 0;

static void k_on_signal(int sig) {
  (void)sig;
  k_quit = 1;
}

static void k_arm_signals(void) {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = k_on_signal;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGHUP, &sa, NULL);
}

static void k_enter_raw(void) {
  struct termios raw;
  if (tcgetattr(STDIN_FILENO, &k_saved_tio) < 0)
    return;
  raw = k_saved_tio;
  cfmakeraw(&raw);
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &raw) < 0)
    return;
  k_raw = true;
}

static void k_leave_raw(void) {
  if (!k_raw)
    return;
  tcsetattr(STDIN_FILENO, TCSADRAIN, &k_saved_tio);
  k_raw = false;
}

static void k_enter_screen(void) {
  k_put("\x1b[?1049h"
        "\x1b[?25l"
        "\x1b[<u"
        "\x1b[>11u");
  k_alt = true;
}

static void k_leave_screen(void) {
  if (!k_alt)
    return;
  k_put("\x1b_Ga=d,d=A,q=2\x1b\\");
  k_put("\x1b[<u");
  k_put("\x1b[?25h\x1b[?1049l");
  k_alt = false;
}

#define K_KB_VERIFY_MS 250

static int k_query_keyboard_flags(void) {
  char buf[128];
  size_t have = 0;
  int waited;
  size_t i, j;
  k_put("\x1b[?u");
  for (waited = 0; waited < K_KB_VERIFY_MS; waited += 10) {
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    ssize_t n;
    if (poll(&pfd, 1, 10) <= 0)
      continue;
    n = read(STDIN_FILENO, buf + have, sizeof buf - have - 1);
    if (n <= 0)
      break;
    have += (size_t)n;
    for (i = 0; i + 2 < have; i++) {
      int val = 0, any = 0;
      if (buf[i] != 0x1b || buf[i + 1] != '[' || buf[i + 2] != '?')
        continue;
      j = i + 3;
      while (j < have && buf[j] >= '0' && buf[j] <= '9') {
        val = val * 10 + (buf[j] - '0');
        any = 1;
        j++;
      }
      if (any && j < have && buf[j] == 'u')
        return val;
    }
  }
  return -1;
}

uint32_t kitty_ticks_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void kitty_delay_ms(uint32_t ms) {
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000u);
  ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
  while (nanosleep(&ts, &ts) < 0 && errno == EINTR)
    ;
}

#define K_IMG_ID 1
#define K_PLACE_ID 1
#define K_NSLOTS 2

struct k_slot {
  char name[64];
  char name_b64[96];
  int fd;
  uint8_t *map;
};

static bool k_video_up_flag = false;
static int k_src_w, k_src_h, k_scale, k_dst_w, k_dst_h;
static size_t k_frame_bytes;
static struct k_slot k_slots[K_NSLOTS];
static int k_cur_slot = 0;

static bool k_read_probe_reply(int id) {
  char buf[512];
  size_t have = 0;
  char target[16];
  int waited;
  snprintf(target, sizeof target, "i=%d;", id);
  size_t tn = strlen(target);
  for (waited = 0; waited < 250; waited += 10) {
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 10) <= 0)
      continue;
    ssize_t n = read(STDIN_FILENO, buf + have, sizeof buf - have - 1);
    if (n <= 0)
      break;
    have += (size_t)n;
    for (size_t i = 0; i + tn + 2 <= have; i++)
      if (memcmp(buf + i, target, tn) == 0 && strncmp(buf + i + tn, "OK", 2) == 0)
        return true;
  }
  return false;
}

// NOTE: 探测 t=s（POSIX shm）介质：建 1x1 探针对象 → a=q 应答 OK 即支持。
// 名字必须带前导 '/'（协议明文）；kitty 读后按协议自 unlink，此处再
// unlink 只是兜底（ENOENT 无害）。shm_open 帧路径零磁盘读写：Linux
// 落 /dev/shm tmpfs（纯内存），macOS 落系统 shm 区（/var/tmp 页缓存，
// 无显式盘 I/O），两端同一 POSIX 名字空间。
static bool k_probe_medium(int id, const char *name) {
  uint8_t px[4] = {0, 0, 0, 255};
  char nb64[96];
  char cmd[256];
  bool ok;
  int n, fd;

  shm_unlink(name);
  fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0)
    return false;
  ok = ftruncate(fd, 4) == 0 && pwrite(fd, px, 4, 0) == 4;
  close(fd);
  if (!ok)
    return false;
  k_b64((const uint8_t *)name, strlen(name), nb64);
  n = snprintf(cmd, sizeof cmd, "\x1b_Gi=%d,a=q,t=s,f=32,s=1,v=1;%s\x1b\\", id, nb64);
  k_write_all(cmd, (size_t)n);
  ok = k_read_probe_reply(id);
  shm_unlink(name);
  return ok;
}

// NOTE: 只登记槽名（名字带前导 '/'，macOS PSHMNAMLEN=31 之内：
// "/pal-frame-<pid>-<n>" ≈ 20 字符）；对象本体每帧在 k_slot_begin 重建
// （kitty 读后按协议自删名字，无法复用旧对象）。shm 可建可扩容先预检
// 一次，失败即 init 失败（对齐旧 file 介质的 fail-fast 语义）。
static bool k_frame_setup(void) {
  pid_t pid = getpid();
  int i, fd;
  bool ok;

  for (i = 0; i < K_NSLOTS; i++) {
    struct k_slot *s = &k_slots[i];
    memset(s, 0, sizeof *s);
    s->fd = -1;
    snprintf(s->name, sizeof s->name, "/pal-frame-%d-%d", (int)pid, i);
    k_b64((const uint8_t *)s->name, strlen(s->name), s->name_b64);
  }
  fd = shm_open(k_slots[0].name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (fd < 0)
    return false;
  ok = ftruncate(fd, (off_t)k_frame_bytes) == 0;
  close(fd);
  shm_unlink(k_slots[0].name);
  return ok;
}

static void k_frame_teardown(void) {
  int i;
  for (i = 0; i < K_NSLOTS; i++) {
    struct k_slot *s = &k_slots[i];
    if (s->map != NULL && s->map != MAP_FAILED)
      munmap(s->map, k_frame_bytes);
    if (s->fd >= 0)
      close(s->fd);
    if (s->name[0] != 0)
      shm_unlink(s->name);
    memset(s, 0, sizeof *s);
    s->fd = -1;
  }
}

static uint8_t *k_slot_begin(int slot) {
  struct k_slot *s = &k_slots[slot];
  shm_unlink(s->name);
  s->fd = shm_open(s->name, O_CREAT | O_EXCL | O_RDWR, 0600);
  if (s->fd < 0)
    return NULL;
  if (ftruncate(s->fd, (off_t)k_frame_bytes) < 0) {
    close(s->fd);
    s->fd = -1;
    return NULL;
  }
  s->map = mmap(NULL, k_frame_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
  if (s->map == MAP_FAILED) {
    s->map = NULL;
    close(s->fd);
    s->fd = -1;
    return NULL;
  }
  return s->map;
}

static void k_slot_end(int slot) {
  struct k_slot *s = &k_slots[slot];
  if (s->map != NULL) {
    munmap(s->map, k_frame_bytes);
    s->map = NULL;
  }
  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }
}

static uint8_t k_linebuf[320 * 4 * 4];

static void k_flush_slot(int slot, bool frame_ok) {
  static const char head[] = "\x1b[?2026h\x1b[H";
  static const char tail[] = "\x1b[?2026l";
  char out[384];
  size_t hl = sizeof head - 1, tl = sizeof tail - 1;
  int n;
  if (frame_ok) {
    n = snprintf(out + hl, sizeof out - hl - tl, "\x1b_Ga=T,f=32,s=%d,v=%d,t=s,i=%d,p=%d,C=1,q=2;%s\x1b\\", k_dst_w,
                 k_dst_h, K_IMG_ID, K_PLACE_ID, k_slots[slot].name_b64);
    if (n > 0 && hl + (size_t)n + tl <= sizeof out) {
      memcpy(out, head, hl);
      memcpy(out + hl + (size_t)n, tail, tl);
      k_write_all(out, hl + (size_t)n + tl);
    }
  }
  k_slot_end(slot);
  k_cur_slot = (k_cur_slot + 1) % K_NSLOTS;
}

void kitty_present_frame(const uint8_t *indexed, const uint32_t lut_xrgb8888[256]) {
  int slot, x, y, sx, sy;
  uint8_t *dst;

  if (!k_video_up_flag || indexed == NULL || lut_xrgb8888 == NULL)
    return;
  slot = k_cur_slot;
  dst = k_slot_begin(slot);
  if (dst != NULL) {
    for (y = 0; y < k_src_h; y++) {
      const uint8_t *srow = indexed + (size_t)y * (size_t)k_src_w;
      for (x = 0; x < k_src_w; x++) {
        uint32_t px = lut_xrgb8888[srow[x]];
        for (sx = 0; sx < k_scale; sx++) {
          uint8_t *q = k_linebuf + ((size_t)x * (size_t)k_scale + (size_t)sx) * 4;
          q[0] = (uint8_t)(px >> 16);
          q[1] = (uint8_t)(px >> 8);
          q[2] = (uint8_t)px;
          q[3] = 0xFF;
        }
      }
      for (sy = 0; sy < k_scale; sy++)
        memcpy(dst + ((size_t)y * (size_t)k_scale + (size_t)sy) * (size_t)k_dst_w * 4, k_linebuf, (size_t)k_dst_w * 4);
    }
  }
  k_flush_slot(slot, dst != NULL);
}

// NOTE: 位复制 5→8bit，与 golden MCI 真彩直绘同色（ffmpeg rgb24 转换零差异）。
// 内存布局同 kitty_present_frame：字节序 [R, G, B, 0xFF]。
static uint32_t k_rgb555_to_rgba(uint16_t p) {
  uint32_t r = (uint32_t)((p >> 10) & 31), g = (uint32_t)((p >> 5) & 31), b = (uint32_t)(p & 31);
  r = (r << 3) | (r >> 2);
  g = (g << 3) | (g >> 2);
  b = (b << 3) | (b >> 2);
  return r | (g << 8) | (b << 16) | 0xFF000000u;
}

void kitty_present_frame_tc(const uint16_t *rgb555le, int src_w, int src_h) {
  int slot, x, y, sx, sy, x0, y0;
  uint8_t *dst;

  if (!k_video_up_flag || rgb555le == NULL || src_w <= 0 || src_h <= 0)
    return;
  if (src_w > k_src_w)
    src_w = k_src_w;
  if (src_h > k_src_h)
    src_h = k_src_h;
  // NOTE: 信箱居中锚定 golden 320×200 模式 put 矩形（AVI 288×180 → (16,10)）。
  x0 = (k_src_w - src_w) / 2;
  y0 = (k_src_h - src_h) / 2;
  slot = k_cur_slot;
  dst = k_slot_begin(slot);
  if (dst != NULL) {
    uint32_t *line = (uint32_t *)k_linebuf;
    for (y = 0; y < k_src_h; y++) {
      for (x = 0; x < k_dst_w; x++)
        line[x] = 0xFF000000u;
      if (y >= y0 && y < y0 + src_h) {
        const uint16_t *srow = rgb555le + (size_t)(y - y0) * (size_t)src_w;
        for (x = 0; x < src_w; x++) {
          uint32_t px = k_rgb555_to_rgba(srow[x]);
          for (sx = 0; sx < k_scale; sx++)
            line[(size_t)(x0 + x) * k_scale + sx] = px;
        }
      }
      for (sy = 0; sy < k_scale; sy++)
        memcpy(dst + ((size_t)y * (size_t)k_scale + (size_t)sy) * (size_t)k_dst_w * 4, k_linebuf, (size_t)k_dst_w * 4);
    }
  }
  k_flush_slot(slot, dst != NULL);
}

int kitty_video_init(int src_w, int src_h, int scale) {
  char probe_shm[48];
  pid_t pid = getpid();

  if (k_video_up_flag)
    return -1;
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
    return -1;
  if (scale < 1)
    scale = 1;
  if (scale > 4)
    scale = 4;

  k_enter_raw();

  snprintf(probe_shm, sizeof probe_shm, "/pal-probe-%d", (int)pid);
  if (!k_probe_medium(13, probe_shm)) {
    k_leave_raw();
    fprintf(stderr, "[kitty] graphics protocol t=s (POSIX shm) unsupported; needs "
                    "kitty directly (remote SSH: no shared shm, kitty.md 8-3); "
                    "exiting\n");
    exit(1);
  }

  k_src_w = src_w;
  k_src_h = src_h;
  k_scale = scale;
  k_dst_w = src_w * scale;
  k_dst_h = src_h * scale;
  k_frame_bytes = (size_t)k_dst_w * (size_t)k_dst_h * 4;

  if (!k_frame_setup()) {
    int slot_errno = errno;
    k_frame_teardown();
    k_leave_raw();
    fprintf(stderr, "[kitty] frame slot setup failed (%s); exiting\n", strerror(slot_errno));
    exit(1);
  }

  k_arm_signals();
  atexit(kitty_terminal_restore);
  k_enter_screen();
  int kb = k_query_keyboard_flags();
  if (kb < 0 || (kb & 11) != 11) {
    k_leave_screen();
    k_frame_teardown();
    fprintf(stderr,
            "[kitty] keyboard protocol not active (%s); needs kitty "
            "directly; exiting\n",
            kb < 0 ? "no CSI ?u reply" : "flags!=11 after push >11u");
    exit(1);
  }
  k_video_up_flag = true;
  return 0;
}

void kitty_video_shutdown(void) {
  if (!k_video_up_flag)
    return;
  k_leave_screen();
  k_frame_teardown();
  k_leave_raw();
  k_video_up_flag = false;
}

bool kitty_video_up(void) { return k_video_up_flag; }

void kitty_terminal_restore(void) {
  if (k_video_up_flag) {
    kitty_video_shutdown();
    return;
  }
  k_leave_screen();
  k_leave_raw();
}

#define K_PENDING_MAX 4096
static uint8_t k_pending[K_PENDING_MAX];
static size_t k_plen = 0;
static bool k_eof = false;
static uint8_t k_down[256];
static uint8_t k_latch[256];
static uint8_t k_snapshot[256];

#define DIK_ESC_ 0x01
#define DIK_TAB 0x0F
#define DIK_ENTER 0x1C
#define DIK_LCTRL 0x1D
#define DIK_LSHIFT 0x2A
#define DIK_SPACE 0x39
#define DIK_LALT 0x38
#define DIK_KPENTER 0x9C
#define DIK_RCTRL 0x9D
#define DIK_RALT 0xB8
#define DIK_UP 0xC8
#define DIK_HOME_ 0xC7
#define DIK_PGUP 0xC9
#define DIK_LEFT 0xCB
#define DIK_RIGHT 0xCD
#define DIK_DOWN 0xD0
#define DIK_END_ 0xCF
#define DIK_PGDN 0xD1
#define DIK_DELETE 0xD3

static int k_letter_dik(uint32_t letter) {
  static const char rows[] = "qwertyuiop"
                             "asdfghjkl"
                             "zxcvbnm";
  static const uint8_t row_first[] = {0x10, 0x1E, 0x2C};
  const char *hit;
  if (letter >= 'A' && letter <= 'Z')
    letter += 32;
  if (letter < 'a' || letter > 'z')
    return 0;
  hit = strchr(rows, (int)letter);
  if (hit == NULL)
    return 0;
  if (hit - rows < 10)
    return row_first[0] + (int)(hit - rows);
  if (hit - rows < 19)
    return row_first[1] + (int)(hit - rows - 10);
  return row_first[2] + (int)(hit - rows - 19);
}

static int k_code_to_dik(uint32_t code) {
  static const struct {
    uint32_t code;
    uint8_t dik;
  } pua[] = {
      {57358, 0x3A},     {57359, 0x46},        {57360, 0x45},     {57376, 0x64},       {57377, 0x65},
      {57378, 0x66},     {57399, 0x52},        {57400, 0x4F},     {57401, 0x50},       {57402, 0x51},
      {57403, 0x4B},     {57404, 0x4C},        {57405, 0x4D},     {57406, 0x47},       {57407, 0x48},
      {57408, 0x49},     {57409, 0x53},        {57410, 0xB5},     {57411, 0x37},       {57412, 0x4A},
      {57413, 0x4E},     {57414, DIK_KPENTER}, {57417, DIK_LEFT}, {57418, DIK_RIGHT},  {57419, DIK_UP},
      {57420, DIK_DOWN}, {57421, DIK_PGUP},    {57422, DIK_PGDN}, {57423, DIK_HOME_},  {57424, DIK_END_},
      {57425, 0x52},     {57426, DIK_DELETE},  {57427, 0x4C},     {57441, DIK_LSHIFT}, {57442, DIK_LCTRL},
      {57443, DIK_LALT}, {57444, 0x5B},        {57447, 0x36},     {57448, DIK_RCTRL},  {57449, DIK_RALT},
      {57450, 0x5C},
  };
  size_t i;
  int d;

  switch (code) {
  case 27:
    return DIK_ESC_;
  case 13:
    return DIK_ENTER;
  case 9:
    return DIK_TAB;
  case 127:
    return 0x0E;
  case ' ':
    return DIK_SPACE;
  case '-':
    return 0x0C;
  case '=':
    return 0x0D;
  case '[':
    return 0x1A;
  case ']':
    return 0x1B;
  case '\\':
    return 0x2B;
  case ';':
    return 0x27;
  case '\'':
    return 0x28;
  case '`':
    return 0x29;
  case ',':
    return 0x33;
  case '.':
    return 0x34;
  case '/':
    return 0x35;
  default:
    break;
  }
  if (code >= '1' && code <= '9')
    return 0x02 + (int)(code - '1');
  if (code == '0')
    return 0x0B;
  d = k_letter_dik(code);
  if (d != 0)
    return d;
  for (i = 0; i < sizeof pua / sizeof pua[0]; i++)
    if (pua[i].code == code)
      return pua[i].dik;
  return 0;
}

static void k_key_event(int dik, int kind) {
  if (dik <= 0)
    return;
  if (kind != 1 && kind != 2 && kind != 3)
    return;
  if (kind == 3) {
    k_down[dik] = 0;
    return;
  }
  if (kind == 2 && k_down[dik])
    return;
  k_down[dik] = 1;
  k_latch[dik] = 1;
}

static void k_consume(size_t n) {
  memmove(k_pending, k_pending + n, k_plen - n);
  k_plen -= n;
}

static int k_param_sub(const uint8_t *p, size_t len, int index, int sub, int def) {
  int cur = 0, sc = 0;
  size_t i = 0, start = 0;
  while (i <= len) {
    if (i == len || p[i] == ';') {
      if (cur == index) {
        size_t s = start, j = start;
        while (j <= i) {
          if (j == i || p[j] == ':') {
            if (sc == sub) {
              int val = 0, any = 0;
              size_t q = s;
              while (q < j) {
                if (p[q] >= '0' && p[q] <= '9') {
                  val = val * 10 + (p[q] - '0');
                  any = 1;
                }
                q++;
              }
              return any ? val : def;
            }
            sc++;
            s = j + 1;
          }
          j++;
        }
        return def;
      }
      cur++;
      start = i + 1;
    }
    i++;
  }
  return def;
}

static int k_param(const uint8_t *p, size_t len, int index, int def) { return k_param_sub(p, len, index, 0, def); }
static int k_csi_letter_dik(uint8_t final) {
  switch (final) {
  case 'A':
    return DIK_UP;
  case 'B':
    return DIK_DOWN;
  case 'C':
    return DIK_RIGHT;
  case 'D':
    return DIK_LEFT;
  case 'H':
    return DIK_HOME_;
  case 'F':
    return DIK_END_;
  case 'E':
    return 0x4C;
  case 'P':
    return 0x3B;
  case 'Q':
    return 0x3C;
  case 'S':
    return 0x3E;
  default:
    return 0;
  }
}

static int k_csi_tilde_dik(int num) {
  static const int map[][2] = {
      {1, DIK_HOME_}, {2, 0x52},  {3, DIK_DELETE}, {4, DIK_END_}, {5, DIK_PGUP}, {6, DIK_PGDN}, {7, DIK_HOME_},
      {8, DIK_END_},  {11, 0x3B}, {12, 0x3C},      {13, 0x3D},    {14, 0x3E},    {15, 0x3F},    {17, 0x40},
      {18, 0x41},     {19, 0x42}, {20, 0x43},      {21, 0x44},    {23, 0x57},    {24, 0x58},
  };
  size_t i;
  for (i = 0; i < sizeof map / sizeof map[0]; i++)
    if (map[i][0] == num)
      return map[i][1];
  return 0;
}

static bool k_parse_one(void) {
  uint8_t b0, b1;
  size_t i;

  if (k_plen == 0)
    return false;
  b0 = k_pending[0];
  if (b0 != 0x1b) {
    k_consume(1);
    return true;
  }

  if (k_plen < 2)
    return false;
  b1 = k_pending[1];

  if (b1 == '[') {
    if (k_plen < 3)
      return false;
    for (i = 2; i < k_plen; i++) {
      uint8_t b = k_pending[i];
      if (b >= 0x40 && b <= 0x7e)
        break;
    }
    if (i >= k_plen) {
      if (k_plen > 512) {
        k_consume(k_plen);
        return true;
      }
      return false;
    }
    uint8_t final = k_pending[i];
    const uint8_t *pp = k_pending + 2;
    size_t pl = i - 2;
    int action = -1;
    int kind = 0;
    switch (final) {
    case 'u': {
      int code = k_param(pp, pl, 0, 0);
      if (code != 0) {
        int mods = k_param(pp, pl, 1, 1);
        int base = k_param_sub(pp, pl, 0, 2, 0);
        kind = k_param_sub(pp, pl, 1, 1, 1);
        if ((mods & 4) && (code == 99 || code == 67 || base == 99)) {
          k_quit = 1;
          break;
        }
        action = k_code_to_dik((uint32_t)code);
      }
      break;
    }
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'H':
    case 'F':
    case 'E':
    case 'P':
    case 'Q':
    case 'S':
      kind = k_param_sub(pp, pl, 1, 1, 1);
      action = k_csi_letter_dik(final);
      break;
    case '~':
      kind = k_param_sub(pp, pl, 1, 1, 1);
      action = k_csi_tilde_dik(k_param(pp, pl, 0, 0));
      break;
    default:
      break;
    }
    k_consume(i + 1);
    if (action >= 0)
      k_key_event(action, kind);
    return true;
  }

  if (b1 == '_' || b1 == 'P' || b1 == 'X' || b1 == '^') {
    for (i = 2; i + 1 < k_plen; i++) {
      if (k_pending[i] == 0x1b && k_pending[i + 1] == '\\') {
        k_consume(i + 2);
        return true;
      }
    }
    if (k_plen > 8192) {
      k_consume(k_plen);
      return true;
    }
    return false;
  }

  k_consume(2);
  return true;
}

void kitty_pump_input(void) {
  while (!k_eof) {
    struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
    uint8_t chunk[256];
    ssize_t n;
    if (poll(&pfd, 1, 0) <= 0)
      break;
    n = read(STDIN_FILENO, chunk, sizeof chunk);
    if (n == 0) {
      k_eof = true;
      break;
    }
    if (n < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (k_plen + (size_t)n > K_PENDING_MAX) {
      size_t drop = k_plen + (size_t)n - K_PENDING_MAX;
      if (drop < k_plen)
        k_consume(drop);
      else
        k_plen = 0;
    }
    memcpy(k_pending + k_plen, chunk, (size_t)n);
    k_plen += (size_t)n;
  }
  while (k_parse_one())
    ;
}

const uint8_t *kitty_key_down_vector(void) {
  int i;
  kitty_pump_input();
  memcpy(k_snapshot, k_down, sizeof k_snapshot);
  for (i = 0; i < 256; i++) {
    if (k_latch[i]) {
      k_snapshot[i] = 1;
      k_latch[i] = 0;
    }
  }
  return k_snapshot;
}

bool kitty_quit_requested(void) { return k_quit != 0; }
