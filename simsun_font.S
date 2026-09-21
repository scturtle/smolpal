/* simsun_font.S — simsun.ttf 内嵌（pal_ext.c R4 文本栈字形源）。
 * .incbin 原字节链入只读段；pal_ext.c 以 pal_simsun_ttf_start/end
 * 取址 + FT_New_Memory_Face 加载，运行时不读盘（simsun.ttf 仅作
 * 构建输入；组装时按 CWD=仓库根定位，make -C 同）。 */
#if defined(__APPLE__)
	.section        __TEXT,__const
#else
	.section        .rodata
#endif
	.global         pal_simsun_ttf_start
	.p2align        4
pal_simsun_ttf_start:
	.incbin         "simsun.ttf"
	.global         pal_simsun_ttf_end
pal_simsun_ttf_end:

#if !defined(__APPLE__)
	.section        .note.GNU-stack,"",@progbits
#endif
