# pal — remake 独立构建：游戏本体 + kitty 终端后端 + miniaudio/adplug 音频
CC       ?= cc
CXX      ?= c++
# 默认开启 AddressSanitizer；make ASAN= 可关闭
ASAN     ?= -fsanitize=address
CFLAGS   ?= -O0 -g -Wall $(ASAN)
CXXFLAGS ?= $(CFLAGS) -std=c++17
LDFLAGS  ?= $(ASAN)

FT_CFLAGS := $(shell pkg-config --cflags freetype2)
FT_LIBS   := $(shell pkg-config --libs freetype2)

# 平台差异：macOS 需显式链接 libiconv、音频走 CoreAudio；
# Linux 上 -lrt 兜底老 glibc，miniaudio 需 -pthread/-ldl
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDLIBS += -liconv
    MA_LIBS := -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework AudioUnit
else
    LDLIBS += -lrt
    MA_LIBS := -pthread -ldl
endif

CPPFLAGS += -I. -Iadplug $(FT_CFLAGS)
LDLIBS   += $(FT_LIBS) -lm $(MA_LIBS)

# adplug（C++）：RIX 播放器 + 基础设施 + OPL 模拟
ADPLUG_OBJS = \
	adplug/rix.o \
	adplug/player.o \
	adplug/binio.o \
	adplug/binfile.o \
	adplug/fprovide.o \
	adplug/woodyopl.o

OBJS = pal.o pal_ext.o avi.o kitty.o audio.o simsun_font.o $(ADPLUG_OBJS) miniaudio/miniaudio.o

# adplug/audio 是 C++，用 $(CXX) 链接以带上 C++ 运行时
pal: $(OBJS)
	$(CXX) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

pal.o: pal.c pal.h
pal_ext.o: pal_ext.c pal.h pal_ext.h avi.h kitty.h
avi.o: avi.c avi.h pal.h kitty.h miniaudio/miniaudio.h
kitty.o: kitty.c kitty.h

# .incbin 内嵌 simsun.ttf，路径相对仓库根，需在仓库根目录 make
simsun_font.o: simsun_font.S simsun.ttf
	$(CC) -c $< -o $@
audio.o: audio.cpp pal.h pal_ext.h miniaudio/miniaudio.h \
           adplug/wemuopl.h adplug/rix.h adplug/fprovide.h adplug/opl.h \
           adplug/woodyopl.h adplug/player.h adplug/binio.h adplug/binfile.h

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

miniaudio/miniaudio.o: miniaudio/miniaudio.c miniaudio/miniaudio.h
	$(CC) $(CFLAGS) -Iminiaudio -c $< -o $@

clean:
	rm -f $(OBJS) pal

.PHONY: clean
