#ifndef TEXUGOS_VIDEO_H
#define TEXUGOS_VIDEO_H

#include <stdint.h>

#define VIDEO_WIDTH 320u
#define VIDEO_HEIGHT 200u

int video_set_graphics_mode(void);
int video_set_text_mode(void);
void video_clear(uint8_t color);
void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color);
void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color);

#endif
