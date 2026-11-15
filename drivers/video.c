#include <stdint.h>

#include "video.h"

#define VGA_MISC_WRITE 0x3c2u
#define VGA_SEQ_INDEX 0x3c4u
#define VGA_SEQ_DATA 0x3c5u
#define VGA_CRTC_INDEX 0x3d4u
#define VGA_CRTC_DATA 0x3d5u
#define VGA_GC_INDEX 0x3ceu
#define VGA_GC_DATA 0x3cfu
#define VGA_ATTR_INDEX_DATA 0x3c0u
#define VGA_INPUT_STATUS 0x3dau
#define VGA_DAC_WRITE_INDEX 0x3c8u
#define VGA_DAC_DATA 0x3c9u
#define VGA_FRAMEBUFFER ((volatile uint8_t *)0xa0000u)
#define VGA_FONT_HEIGHT 16u
#define VGA_FONT_STRIDE 32u
#define VGA_FONT_CHARACTERS 256u

static const uint8_t mode13_sequencer[5] = {
    0x03, 0x01, 0x0f, 0x00, 0x0e,
};
static const uint8_t mode13_crtc[25] = {
    0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f,
    0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x9c, 0x0e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff,
};
static const uint8_t mode13_graphics[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff,
};
static const uint8_t mode13_attribute[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x41, 0x00, 0x0f, 0x00, 0x00,
};

static const uint8_t mode3_sequencer[5] = {
    0x03, 0x00, 0x03, 0x00, 0x02,
};
static const uint8_t mode3_crtc[25] = {
    0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f,
    0x00, 0x4f, 0x0d, 0x0e, 0x00, 0x00, 0x00, 0x50,
    0x9c, 0x0e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff,
};
static const uint8_t mode3_graphics[9] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff,
};
static const uint8_t mode3_attribute[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x0c, 0x00, 0x0f, 0x08, 0x00,
};

static const uint8_t desktop_palette[16][3] = {
    {2, 4, 8},   {4, 7, 11},   {7, 11, 17},  {11, 16, 24},
    {56, 22, 26}, {41, 34, 62}, {63, 47, 21}, {49, 52, 55},
    {20, 24, 30}, {20, 33, 61}, {14, 50, 34}, {17, 54, 57},
    {51, 30, 57}, {60, 34, 42}, {63, 52, 25}, {61, 62, 63},
};

static const uint8_t font[36][7] = {
    {0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}, /* A */
    {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}, /* B */
    {0x0e,0x11,0x10,0x10,0x10,0x11,0x0e}, /* C */
    {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}, /* D */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}, /* E */
    {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}, /* F */
    {0x0e,0x11,0x10,0x17,0x11,0x11,0x0f}, /* G */
    {0x11,0x11,0x11,0x1f,0x11,0x11,0x11}, /* H */
    {0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}, /* I */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0c}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1f}, /* L */
    {0x11,0x1b,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}, /* O */
    {0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}, /* P */
    {0x0e,0x11,0x11,0x11,0x15,0x12,0x0d}, /* Q */
    {0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}, /* R */
    {0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}, /* S */
    {0x1f,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0e}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0a,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0a}, /* W */
    {0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}, /* X */
    {0x11,0x11,0x0a,0x04,0x04,0x04,0x04}, /* Y */
    {0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}, /* Z */
    {0x0e,0x11,0x13,0x15,0x19,0x11,0x0e}, /* 0 */
    {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e}, /* 1 */
    {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f}, /* 2 */
    {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e}, /* 3 */
    {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02}, /* 4 */
    {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e}, /* 5 */
    {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e}, /* 6 */
    {0x1f,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}, /* 8 */
    {0x0e,0x11,0x11,0x0f,0x01,0x01,0x0e}, /* 9 */
};

static uint8_t saved_text_font[VGA_FONT_CHARACTERS * VGA_FONT_HEIGHT];
static uint8_t text_font_saved;

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t in8(uint16_t port)
{
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void write_indexed(uint16_t index_port, uint16_t data_port,
                          uint8_t index, uint8_t value)
{
    out8(index_port, index);
    out8(data_port, value);
}

static void set_mode(uint8_t misc, const uint8_t *sequencer,
                     const uint8_t *crtc, const uint8_t *graphics,
                     const uint8_t *attribute)
{
    out8(VGA_MISC_WRITE, misc);
    for (uint8_t index = 0; index < 5u; ++index) {
        write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, index, sequencer[index]);
    }

    write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x03u,
                  (uint8_t)(crtc[3] | 0x80u));
    write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, 0x11u,
                  (uint8_t)(crtc[0x11] & 0x7fu));
    for (uint8_t index = 0; index < 25u; ++index) {
        write_indexed(VGA_CRTC_INDEX, VGA_CRTC_DATA, index, crtc[index]);
    }
    for (uint8_t index = 0; index < 9u; ++index) {
        write_indexed(VGA_GC_INDEX, VGA_GC_DATA, index, graphics[index]);
    }
    for (uint8_t index = 0; index < 21u; ++index) {
        (void)in8(VGA_INPUT_STATUS);
        out8(VGA_ATTR_INDEX_DATA, index);
        out8(VGA_ATTR_INDEX_DATA, attribute[index]);
    }
    (void)in8(VGA_INPUT_STATUS);
    out8(VGA_ATTR_INDEX_DATA, 0x20u);
}

static void set_desktop_palette(void)
{
    out8(VGA_DAC_WRITE_INDEX, 0u);
    for (uint32_t color = 0u; color < 16u; ++color) {
        for (uint32_t channel = 0u; channel < 3u; ++channel) {
            out8(VGA_DAC_DATA, desktop_palette[color][channel]);
        }
    }
}

static void select_text_font_plane(void)
{
    write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x02u, 0x04u);
    write_indexed(VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x04u, 0x07u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x04u, 0x02u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x05u, 0x00u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x06u, 0x04u);
    write_indexed(VGA_GC_INDEX, VGA_GC_DATA, 0x08u, 0xffu);
}

static void save_text_font(void)
{
    select_text_font_plane();
    for (uint32_t character = 0u; character < VGA_FONT_CHARACTERS;
         ++character) {
        for (uint32_t row = 0u; row < VGA_FONT_HEIGHT; ++row) {
            saved_text_font[character * VGA_FONT_HEIGHT + row] =
                VGA_FRAMEBUFFER[character * VGA_FONT_STRIDE + row];
        }
    }
    text_font_saved = 1u;
}

static void restore_text_font(void)
{
    if (!text_font_saved) {
        return;
    }

    select_text_font_plane();
    for (uint32_t character = 0u; character < VGA_FONT_CHARACTERS;
         ++character) {
        for (uint32_t row = 0u; row < VGA_FONT_HEIGHT; ++row) {
            VGA_FRAMEBUFFER[character * VGA_FONT_STRIDE + row] =
                saved_text_font[character * VGA_FONT_HEIGHT + row];
        }
    }
    set_mode(0x67u, mode3_sequencer, mode3_crtc,
             mode3_graphics, mode3_attribute);
}

int video_set_graphics_mode(void)
{
    save_text_font();
    set_mode(0x63u, mode13_sequencer, mode13_crtc,
             mode13_graphics, mode13_attribute);
    set_desktop_palette();
    video_clear(0u);
    return 1;
}

int video_set_text_mode(void)
{
    set_mode(0x67u, mode3_sequencer, mode3_crtc,
             mode3_graphics, mode3_attribute);
    restore_text_font();
    return 1;
}

void video_clear(uint8_t color)
{
    for (uint32_t index = 0; index < VIDEO_WIDTH * VIDEO_HEIGHT; ++index) {
        VGA_FRAMEBUFFER[index] = color;
    }
}

void video_fill_rect(int32_t x, int32_t y, uint32_t width, uint32_t height,
                     uint8_t color)
{
    for (uint32_t row = 0; row < height; ++row) {
        int32_t py = y + (int32_t)row;
        if (py < 0 || py >= (int32_t)VIDEO_HEIGHT) {
            continue;
        }
        for (uint32_t column = 0; column < width; ++column) {
            int32_t px = x + (int32_t)column;
            if (px >= 0 && px < (int32_t)VIDEO_WIDTH) {
                VGA_FRAMEBUFFER[(uint32_t)py * VIDEO_WIDTH + (uint32_t)px] =
                    color;
            }
        }
    }
}

void video_fill_circle(int32_t center_x, int32_t center_y, uint32_t radius,
                       uint8_t color)
{
    int32_t signed_radius = (int32_t)radius;
    int32_t radius_squared = signed_radius * signed_radius;
    for (int32_t offset_y = -signed_radius;
         offset_y <= signed_radius; ++offset_y) {
        int32_t extent = 0;
        int32_t remaining =
            radius_squared - offset_y * offset_y;
        while ((extent + 1) * (extent + 1) <= remaining) {
            ++extent;
        }
        video_fill_rect(center_x - extent, center_y + offset_y,
                        (uint32_t)(extent * 2 + 1), 1u, color);
    }
}

static void fill_round_rect(int32_t x, int32_t y, uint32_t width,
                            uint32_t height, uint32_t radius, uint8_t color)
{
    if (width <= radius * 2u || height <= radius * 2u) {
        video_fill_rect(x, y, width, height, color);
        return;
    }
    video_fill_rect(x + (int32_t)radius, y, width - radius * 2u, height,
                    color);
    video_fill_rect(x, y + (int32_t)radius, width, height - radius * 2u,
                    color);
    video_fill_circle(x + (int32_t)radius, y + (int32_t)radius, radius,
                      color);
    video_fill_circle(x + (int32_t)width - (int32_t)radius - 1,
                      y + (int32_t)radius, radius, color);
    video_fill_circle(x + (int32_t)radius,
                      y + (int32_t)height - (int32_t)radius - 1,
                      radius, color);
    video_fill_circle(x + (int32_t)width - (int32_t)radius - 1,
                      y + (int32_t)height - (int32_t)radius - 1,
                      radius, color);
}

static const uint8_t *glyph_for(char character)
{
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }
    if (character >= 'A' && character <= 'Z') {
        return font[(uint32_t)(character - 'A')];
    }
    if (character >= '0' && character <= '9') {
        return font[26u + (uint32_t)(character - '0')];
    }
    return 0;
}

static uint32_t text_width(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length * 6u;
}

void video_draw_text(int32_t x, int32_t y, const char *text, uint8_t color)
{
    while (*text != '\0') {
        const uint8_t *glyph = glyph_for(*text);
        if (glyph != 0) {
            for (uint32_t row = 0; row < 7u; ++row) {
                for (uint32_t column = 0; column < 5u; ++column) {
                    if ((glyph[row] & (1u << (4u - column))) != 0u) {
                        video_fill_rect(x + (int32_t)column,
                                        y + (int32_t)row, 1u, 1u, color);
                    }
                }

            }
        } else if (*text == '.') {
            video_fill_rect(x + 2, y + 6, 1u, 1u, color);
        } else if (*text == ':') {
            video_fill_rect(x + 2, y + 2, 1u, 1u, color);
            video_fill_rect(x + 2, y + 5, 1u, 1u, color);
        } else if (*text == '-') {
            video_fill_rect(x + 1, y + 3, 3u, 1u, color);
        } else if (*text == '/') {
            video_fill_rect(x + 3, y + 1, 1u, 1u, color);
            video_fill_rect(x + 2, y + 2, 1u, 1u, color);
            video_fill_rect(x + 2, y + 3, 1u, 1u, color);
            video_fill_rect(x + 1, y + 4, 1u, 1u, color);
            video_fill_rect(x + 1, y + 5, 1u, 1u, color);
        }
        x += 6;
        ++text;
    }
}

void video_draw_button(int32_t x, int32_t y, uint32_t width, uint32_t height,
                       const char *label, uint8_t background,
                       uint8_t foreground)
{
    if (width < 10u || height < 9u) {
        return;
    }
    fill_round_rect(x + 1, y + 2, width, height, 2u, 0u);
    fill_round_rect(x, y, width, height, 2u, 8u);
    fill_round_rect(x + 1, y + 1, width - 2u, height - 2u,
                    2u, background);
    video_fill_rect(x + 4, y + 1, width - 8u, 1u, 11u);
    video_fill_rect(x + 4, y + (int32_t)height - 2,
                    width - 8u, 1u, 3u);

    uint32_t label_width = text_width(label);
    if (label_width <= width - 6u) {
        int32_t label_x = x + (int32_t)(width - label_width) / 2;
        int32_t label_y = y + ((int32_t)height - 7) / 2;
        video_draw_text(label_x, label_y, label, foreground);
    }
}
