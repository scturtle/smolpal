// NOTE: remake 增补的平台服务契约头（golden 无对应），pal_ext.c/audio.cpp 的过渡层。

#ifndef PAL_EXT_H
#define PAL_EXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool BOOL;
#define TRUE 1
#define FALSE 0

// NOTE: 契约沿用 SDLPAL audio.h；AUDIO_StopMusic 为 remake 增补，
// 承接 golden midi_close()/cd_stopplay() 的 MCI stop/close 语义。
int AUDIO_Init(void);
void AUDIO_Shutdown(void);
void AUDIO_PlayMusic(int numRIX, BOOL loop, float fadeTime);
void AUDIO_StopMusic(void);
int AUDIO_MusicPlaying(void);
void AUDIO_PlaySound(int soundNum);

#ifdef __cplusplus
}
#endif

#endif
