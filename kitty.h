// NOTE: remake 增补的终端后端契约，golden 无对应（原版呈现/输入 = GDI/DirectDraw/DirectInput）。

#ifndef KITTY_H
#define KITTY_H

#include <stdbool.h>
#include <stdint.h>

int kitty_video_init(int src_w, int src_h, int scale);
void kitty_video_shutdown(void);
bool kitty_video_up(void);

void kitty_present_frame(const uint8_t *indexed, const uint32_t lut_xrgb8888[256]);
// NOTE: 真彩直呈（golden 无对应；SDLPAL VIDEO_DrawSurfaceToScreen 等价物）：
// rgb555le 子帧居中于 320×200 逻辑面，×scale 最近邻上屏，不经 8-bit 调色板。
void kitty_present_frame_tc(const uint16_t *rgb555le, int src_w, int src_h);

void kitty_pump_input(void);
const uint8_t *kitty_key_down_vector(void);
bool kitty_quit_requested(void);

uint32_t kitty_ticks_ms(void);
void kitty_delay_ms(uint32_t ms);
void kitty_terminal_restore(void);

#endif
