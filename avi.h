// NOTE: remake 增补的 AVI 播放接口——golden 经 MCI avivideo 播放
// （pal_dll.c PAL_PlayAvi 0x1cf2），remake 自持解码，实现见 avi.c。
#ifndef AVI_H
#define AVI_H

// NOTE: 偏离 golden：N.AVI 只在数据目录找（$PAL98_DATA，未设即 CWD；
// golden 为 CWD → 游戏光盘回退）；skippable = golden stop==0 路径（~1s 后
// 任意键可跳），0 = stop!=0（播完为止）；SIGINT/SIGTERM 恒可退。
// 返回 0 = 已播出（含跳过）；-1 = 未播。
int AVI_Play(int number, int skippable);

#endif
