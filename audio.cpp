// NOTE: remake 增补音频后端：golden 音乐走 MCI MIDI（MUSICS\NNN.MID）、音效由
// DirectSound 预载；remake 改放 MUS.MKF 的 RIX + Sounds.mkf 首播惰性解码（miniaudio）。

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pal.h"
#include "pal_ext.h"

#include "miniaudio/miniaudio.h"

#include "adplug/fprovide.h"
#include "adplug/rix.h"
#include "adplug/wemuopl.h"

#define AUD_RATE 44100
#define AUD_CHANNELS 2
#define SAMPLES_PER_TICK (AUD_RATE / 70)
#define MAX_VOICES 16
#define CMD_QUEUE_SIZE 64

enum { CMD_STOP_MUSIC = 0, CMD_PLAY_MUSIC, CMD_PLAY_SOUND };

struct AudioCmd {
  int type;
  int a;
  int b;
};

static AudioCmd g_cmds[CMD_QUEUE_SIZE];
static std::atomic<uint32_t> g_cmd_head{0};
static std::atomic<uint32_t> g_cmd_tail{0};

static void push_cmd(int type, int a, int b) {
  uint32_t t = g_cmd_tail.load(std::memory_order_relaxed);
  if (t - g_cmd_head.load(std::memory_order_acquire) >= CMD_QUEUE_SIZE)
    return;
  AudioCmd &c = g_cmds[t & (CMD_QUEUE_SIZE - 1)];
  c.type = type;
  c.a = a;
  c.b = b;
  g_cmd_tail.store(t + 1, std::memory_order_release);
}

static ma_device g_device;
static bool g_device_ok;

static CWemuopl *g_opl;
static CrixPlayer *g_rix;
static bool g_music_on;
static int g_music_song;
static bool g_music_loop;
static short g_tick_buf[SAMPLES_PER_TICK * AUD_CHANNELS];
static uint32_t g_tick_pos;

struct SoundSample {
  bool loaded, valid;
  int16_t *data;
  uint32_t frames;
  int rate;
};

#define SND_MAX 512

static SoundSample g_snd[SND_MAX];
static int32_t g_snd_fh = -1;
static std::vector<uint32_t> g_snd_table;

struct Voice {
  bool active;
  const int16_t *data;
  uint32_t frames;
  uint64_t pos;
  uint64_t step;
};

static Voice g_voices[MAX_VOICES];
static uint32_t g_voice_rr;

class CMemStream : public binistream {
public:
  CMemStream(const uint8_t *buf, long size) : buf(buf), size(size), p(0) {
    setFlag(binio::BigEndian, false);
    setFlag(binio::FloatIEEE);
  }

protected:
  void seek(long pos, Offset whence) override {
    switch (whence) {
    case Set:
      p = pos;
      break;
    case Add:
      p += pos;
      break;
    case End:
      p = size + pos;
      break;
    }
    if (p < 0)
      p = 0;
    if (p > size)
      p = size;
  }

  long pos() override { return p; }

  Byte getByte() override {
    if (p >= size) {
      err = Eof;
      return 0;
    }
    return buf[p++];
  }

private:
  const uint8_t *buf;
  long size;
  long p;
};

class CMemProvider : public CFileProvider {
public:
  CMemProvider(const uint8_t *data, long size) : data(data), size(size) {}

  binistream *open(std::string) const override { return new CMemStream(data, size); }
  void close(binistream *f) const override { delete f; }

private:
  const uint8_t *data;
  long size;
};

static inline int16_t sat_add(int16_t a, int b) {
  int v = a + b;
  if (v > 32767)
    v = 32767;
  if (v < -32768)
    v = -32768;
  return (int16_t)v;
}

static bool next_tick(void) {
  if (g_rix == nullptr || g_opl == nullptr)
    return false;
  if (!g_rix->update()) {
    if (!g_music_loop) {
      g_music_on = false;
      return false;
    }
    g_rix->rewind(g_music_song);
    if (!g_rix->update()) {
      g_music_on = false;
      return false;
    }
  }
  g_opl->update(g_tick_buf, SAMPLES_PER_TICK);
  g_tick_pos = 0;
  return true;
}

static void music_start(int song, int loop) {
  if (g_rix == nullptr)
    return;
  if (song < 1) {
    g_music_on = false;
    g_music_song = -1;
    return;
  }
  g_rix->rewind(song);
  g_music_song = song;
  g_music_loop = loop != 0;
  g_music_on = true;
  g_tick_pos = SAMPLES_PER_TICK;
}

static void music_stop(void) {
  g_music_on = false;
  g_music_song = -1;
}

static void mix_music(int16_t *out, uint32_t frameCount) {
  uint32_t done = 0;
  while (g_music_on && done < frameCount) {
    if (g_tick_pos >= SAMPLES_PER_TICK && !next_tick())
      break;
    uint32_t n = SAMPLES_PER_TICK - g_tick_pos;
    if (n > frameCount - done)
      n = frameCount - done;
    const short *src = g_tick_buf + g_tick_pos * AUD_CHANNELS;
    int16_t *dst = out + done * AUD_CHANNELS;
    for (uint32_t i = 0; i < n; i++) {
      dst[i * 2 + 0] = sat_add(dst[i * 2 + 0], src[i * 2 + 0]);
      dst[i * 2 + 1] = sat_add(dst[i * 2 + 1], src[i * 2 + 1]);
    }
    g_tick_pos += n;
    done += n;
  }
}

static void voice_start(int soundNum) {
  if (soundNum < 0 || soundNum >= SND_MAX || !g_snd[soundNum].valid)
    return;
  const SoundSample &s = g_snd[soundNum];

  Voice *v = nullptr;
  for (Voice &cand : g_voices)
    if (!cand.active) {
      v = &cand;
      break;
    }
  if (v == nullptr)
    v = &g_voices[(g_voice_rr++) % MAX_VOICES];

  v->data = s.data;
  v->frames = s.frames;
  v->pos = 0;
  v->step = ((uint64_t)(uint32_t)s.rate << 32) / AUD_RATE;
  v->active = true;
}

static void mix_one_voice(Voice *v, int16_t *out, uint32_t frameCount) {
  uint64_t pos = v->pos;
  uint32_t i = 0;
  while (i < frameCount) {
    uint32_t ip = (uint32_t)(pos >> 32);
    if (ip + 1 >= v->frames) {
      v->active = false;
      return;
    }
    int32_t s0 = v->data[ip], s1 = v->data[ip + 1];
    uint32_t frac = (uint32_t)(pos & 0xffffffffu) >> 17;
    int32_t sample = s0 + (((s1 - s0) * (int32_t)frac) >> 15);
    out[i * 2 + 0] = sat_add(out[i * 2 + 0], (int16_t)sample);
    out[i * 2 + 1] = sat_add(out[i * 2 + 1], (int16_t)sample);
    pos += v->step;
    i++;
  }
  v->pos = pos;
}

static void mix_voices(int16_t *out, uint32_t frameCount) {
  for (Voice &v : g_voices)
    if (v.active)
      mix_one_voice(&v, out, frameCount);
}

static void audio_data_callback(ma_device *device, void *output, const void *input, ma_uint32 frameCount) {
  (void)device;
  (void)input;
  int16_t *out = (int16_t *)output;

  uint32_t h = g_cmd_head.load(std::memory_order_relaxed);
  uint32_t t = g_cmd_tail.load(std::memory_order_acquire);
  while (h != t) {
    const AudioCmd &c = g_cmds[h & (CMD_QUEUE_SIZE - 1)];
    if (c.type == CMD_PLAY_MUSIC)
      music_start(c.a, c.b);
    else if (c.type == CMD_STOP_MUSIC)
      music_stop();
    else if (c.type == CMD_PLAY_SOUND)
      voice_start(c.a);
    h++;
    g_cmd_head.store(h, std::memory_order_release);
    t = g_cmd_tail.load(std::memory_order_acquire);
  }

  memset(out, 0, (size_t)frameCount * AUD_CHANNELS * sizeof(int16_t));
  mix_music(out, frameCount);
  mix_voices(out, frameCount);
}

static bool sounds_open(void) {
  int32_t fh = pal_lopen("Sounds.mkf", 0);
  if (fh <= 0)
    return false;

  uint32_t first;
  if (pal_llseek(fh, 0, SEEK_SET) < 0 || pal_hread(fh, &first, 4) != 4 || first < 4 || first % 4 != 0 ||
      first / 4 > SND_MAX) {
    pal_lclose(fh);
    return false;
  }
  uint32_t entries = first / 4;
  g_snd_table.resize(entries);
  memcpy(&g_snd_table[0], &first, 4);
  if (pal_hread(fh, &g_snd_table[1], (int32_t)((entries - 1) * 4)) != (int32_t)((entries - 1) * 4)) {
    g_snd_table.clear();
    pal_lclose(fh);
    return false;
  }
  g_snd_fh = fh;
  return true;
}

static bool sound_load(int soundNum) {
  SoundSample &s = g_snd[soundNum];
  if (s.loaded)
    return s.valid;
  s.loaded = true;

  if (g_snd_fh <= 0 || soundNum < 1 || (uint32_t)soundNum + 1 >= g_snd_table.size())
    return false;
  uint32_t start = g_snd_table[soundNum];
  uint32_t end = g_snd_table[soundNum + 1];
  if (end <= start || end - start > (64u << 20))
    return false;

  std::vector<uint8_t> buf(end - start);
  if (pal_llseek(g_snd_fh, (int32_t)start, SEEK_SET) < 0 ||
      (uint32_t)pal_hread(g_snd_fh, buf.data(), (int32_t)buf.size()) != buf.size())
    return false;

  ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 0);
  ma_decoder dec;
  if (ma_decoder_init_memory(buf.data(), buf.size(), &cfg, &dec) != MA_SUCCESS)
    return false;

  ma_uint64 frames = 0;
  ma_decoder_get_length_in_pcm_frames(&dec, &frames);
  if (frames == 0 || frames > (64u << 20)) {
    ma_decoder_uninit(&dec);
    return false;
  }

  s.data = (int16_t *)malloc((size_t)frames * sizeof(int16_t));
  if (s.data == nullptr) {
    ma_decoder_uninit(&dec);
    return false;
  }
  s.frames = (uint32_t)frames;
  s.rate = (int)dec.outputSampleRate;
  ma_uint64 got = 0;
  ma_decoder_read_pcm_frames(&dec, s.data, (ma_uint64)frames, &got);
  ma_decoder_uninit(&dec);
  if (got != frames) {
    free(s.data);
    s.data = nullptr;
    return false;
  }
  s.valid = true;
  return true;
}

static bool music_open(void) {
  int32_t fh = pal_lopen("MUS.MKF", 0);
  if (fh <= 0)
    return false;
  int32_t size = pal_llseek(fh, 0, SEEK_END);
  if (size <= 0) {
    pal_lclose(fh);
    return false;
  }
  std::vector<uint8_t> buf((size_t)size);
  pal_llseek(fh, 0, SEEK_SET);
  bool ok = (uint32_t)pal_hread(fh, buf.data(), size) == (uint32_t)size;
  pal_lclose(fh);
  if (!ok)
    return false;

  CMemProvider provider(buf.data(), (long)size);
  return g_rix->load("MUS.MKF", provider);
}

int AUDIO_Init(void) {
  if (g_device_ok)
    return 0;

  memset(g_voices, 0, sizeof(g_voices));
  g_music_on = false;
  g_music_song = -1;

  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_s16;
  cfg.playback.channels = AUD_CHANNELS;
  cfg.sampleRate = AUD_RATE;
  cfg.dataCallback = audio_data_callback;

  if (ma_device_init(NULL, &cfg, &g_device) != MA_SUCCESS) {
    fprintf(stderr, "[audio] miniaudio 设备初始化失败，无声运行"
                    "（golden 无声卡路径：music_mode=0/has_sfx_raw=0）\n");
    return 1;
  }
  if (ma_device_start(&g_device) != MA_SUCCESS) {
    ma_device_uninit(&g_device);
    return 1;
  }
  g_device_ok = true;

  g_opl = new CWemuopl(AUD_RATE, true, true);
  g_rix = new CrixPlayer(g_opl);
  if (!music_open()) {
    fprintf(stderr, "[audio] MUS.MKF 不可用，音乐禁用（音效不受影响）\n");
    delete g_rix;
    g_rix = nullptr;
  }
  if (!sounds_open())
    fprintf(stderr, "[audio] Sounds.mkf 不可用，音效禁用（音乐不受影响）\n");

  return 0;
}

void AUDIO_Shutdown(void) {
  if (g_device_ok) {
    ma_device_uninit(&g_device);
    g_device_ok = false;
  }
  delete g_rix;
  g_rix = nullptr;
  delete g_opl;
  g_opl = nullptr;
  g_music_on = false;
  g_music_song = -1;
  if (g_snd_fh > 0) {
    pal_lclose(g_snd_fh);
    g_snd_fh = -1;
  }
  for (SoundSample &s : g_snd) {
    free(s.data);
    s.data = nullptr;
    s.frames = 0;
    s.rate = 0;
    s.loaded = s.valid = false;
  }
  g_snd_table.clear();
  memset(g_voices, 0, sizeof(g_voices));
  g_cmd_head.store(0, std::memory_order_relaxed);
  g_cmd_tail.store(0, std::memory_order_relaxed);
}

void AUDIO_PlayMusic(int numRIX, BOOL loop, float fadeTime) {
  (void)fadeTime;
  if (!g_device_ok)
    return;
  push_cmd(CMD_PLAY_MUSIC, numRIX, loop ? 1 : 0);
}

void AUDIO_StopMusic(void) {
  if (!g_device_ok)
    return;
  push_cmd(CMD_STOP_MUSIC, 0, 0);
}

int AUDIO_MusicPlaying(void) { return (g_music_on && g_music_song > 0) ? 1 : 0; }

void AUDIO_PlaySound(int soundNum) {
  if (!g_device_ok)
    return;
  if (soundNum < 1 || soundNum >= (int)g_snd_table.size() - 1)
    return;
  if (!sound_load(soundNum))
    return;
  push_cmd(CMD_PLAY_SOUND, soundNum, 0);
}
