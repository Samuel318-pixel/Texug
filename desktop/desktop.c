#include <stdint.h>

#include "console.h"
#include "desktop.h"
#include "keyboard.h"
#include "mouse.h"
#include "pit.h"
#include "video.h"

#define WINDOW_COUNT 3u
#define TASKBAR_Y 184
#define WINDOW_MIN_WIDTH 112u
#define WINDOW_MIN_HEIGHT 72u

struct desktop_window {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint8_t open;
    uint8_t minimized;
    uint8_t maximized;
};

enum drag_mode {
    DRAG_NONE,
    DRAG_TITLE,
    DRAG_RESIZE
};

static const char *const window_titles[WINDOW_COUNT] = {
    "FILES", "TERMINAL", "SETTINGS",
};
static struct desktop_window windows[WINDOW_COUNT];
static int32_t active_window = -1;
static uint8_t menu_open;
static enum drag_mode dragging;
static int32_t drag_offset_x;
static int32_t drag_offset_y;

static int32_t clamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static void draw_icon(uint32_t index)
{
    int32_t x = 14;
    int32_t y = 22 + (int32_t)index * 48;
    uint8_t color = index == 0u ? 14u : (index == 1u ? 11u : 10u);
    video_fill_rect(x, y, 22u, 20u, 15u);
    video_fill_rect(x + 2, y + 2, 18u, 16u, color);
    if (index == 0u) {
        video_fill_rect(x + 5, y + 6, 12u, 9u, 15u);
        video_fill_rect(x + 5, y + 5, 6u, 2u, 15u);
    } else if (index == 1u) {
        video_fill_rect(x + 5, y + 5, 12u, 10u, 1u);
        video_fill_rect(x + 7, y + 7, 8u, 1u, 15u);
        video_fill_rect(x + 7, y + 10, 6u, 1u, 15u);
    } else {
        video_fill_rect(x + 6, y + 4, 10u, 12u, 15u);
        video_fill_rect(x + 9, y + 7, 4u, 6u, color);
    }

    video_draw_text(6, y + 23, window_titles[index], 15u);
}

static void draw_wallpaper(void)
{
    video_clear(1u);
    for (uint32_t y = 0; y < (uint32_t)TASKBAR_Y; ++y) {
        uint8_t color = y < 70u ? 1u : (y < 132u ? 9u : 1u);
        video_fill_rect(0, (int32_t)y, VIDEO_WIDTH, 1u, color);
    }
    video_fill_rect(176, 20, 118u, 118u, 9u);
    video_fill_rect(188, 32, 118u, 118u, 11u);
    video_fill_rect(200, 44, 118u, 118u, 1u);
    video_fill_rect(214, 58, 84u, 84u, 11u);
    video_fill_rect(228, 72, 56u, 56u, 9u);

    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        draw_icon(index);
    }

    video_fill_rect(0, TASKBAR_Y, VIDEO_WIDTH, VIDEO_HEIGHT - TASKBAR_Y, 0u);
    video_fill_rect(0, TASKBAR_Y, VIDEO_WIDTH, 1u, 8u);
    video_fill_rect(5, TASKBAR_Y + 3, 47u, 11u, 9u);
    video_draw_text(10, TASKBAR_Y + 5, "START", 15u);

    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        if (windows[index].open && !windows[index].minimized) {
            int32_t button_x = 57 + (int32_t)index * 56;
            video_fill_rect(button_x, TASKBAR_Y + 3, 52u, 11u,
                            (int32_t)index == active_window ? 9u : 8u);
            video_draw_text(button_x + 4, TASKBAR_Y + 5,
                            window_titles[index], 15u);
        }
    }

    uint32_t seconds = pit_ticks_read() / 100u;
    char clock_text[6] = {
        (char)('0' + (seconds / 60u) % 10u),
        (char)('0' + (seconds / 10u) % 6u),
        ':',
        (char)('0' + (seconds % 60u) / 10u),
        (char)('0' + seconds % 10u),
        '\0',
    };
    video_draw_text(278, TASKBAR_Y + 5, clock_text, 15u);
}

static void draw_window(uint32_t index)
{
    struct desktop_window *window = &windows[index];
    if (!window->open || window->minimized) {
        return;
    }

    int32_t x = window->x;
    int32_t y = window->y;
    uint32_t width = window->width;
    uint32_t height = window->height;
    uint8_t title_color = (int32_t)index == active_window ? 9u : 8u;

    video_fill_rect(x, y, width, height, 15u);
    video_fill_rect(x + 1, y + 1, width - 2u, height - 2u, 7u);
    video_fill_rect(x + 2, y + 2, width - 4u, 13u, title_color);
    video_draw_text(x + 6, y + 5, window_titles[index], 15u);

    video_fill_rect(x + (int32_t)width - 34, y + 4, 8u, 8u, 8u);
    video_fill_rect(x + (int32_t)width - 32, y + 8, 4u, 1u, 15u);
    video_fill_rect(x + (int32_t)width - 23, y + 4, 8u, 8u, 8u);
    video_fill_rect(x + (int32_t)width - 21, y + 6, 4u, 4u, 15u);
    video_fill_rect(x + (int32_t)width - 12, y + 4, 8u, 8u, 4u);
    video_draw_text(x + (int32_t)width - 11, y + 5, "X", 15u);

    video_fill_rect(x + 3, y + 17, width - 6u, height - 20u, 15u);
    video_draw_text(x + 10, y + 26, "APP NOT INSTALLED", 1u);
    video_draw_text(x + 10, y + 39, "NATIVE APPS ARE", 1u);
    video_draw_text(x + 10, y + 50, "PLANNED FOR", 1u);
    video_draw_text(x + 10, y + 61, "STAGE 8", 1u);
    video_draw_text(x + 10, y + 76, "ESC CLOSES DESKTOP", 8u);
    video_fill_rect(x + (int32_t)width - 8,
                    y + (int32_t)height - 8, 4u, 4u, 8u);
}

static void draw_start_menu(void)
{
    if (!menu_open) {
        return;
    }
    video_fill_rect(4, 104, 116u, 78u, 15u);
    video_fill_rect(6, 106, 112u, 74u, 1u);
    video_draw_text(13, 112, "TEXUGOS", 15u);
    video_draw_text(13, 130, "FILES", 15u);
    video_draw_text(13, 145, "TERMINAL", 15u);
    video_draw_text(13, 160, "SETTINGS", 15u);
}

static void draw_cursor(uint32_t x, uint32_t y)
{
    static const uint8_t shape[12][2] = {
        {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5},
        {1, 5}, {2, 6}, {3, 7}, {4, 8}, {2, 5}, {1, 4},
    };
    for (uint32_t index = 0; index < 12u; ++index) {
        video_fill_rect((int32_t)x + shape[index][0],
                        (int32_t)y + shape[index][1], 1u, 1u, 0u);
    }
    for (uint32_t index = 0; index < 6u; ++index) {
        video_fill_rect((int32_t)x + (int32_t)index,
                        (int32_t)y + (int32_t)index, 1u, 1u, 15u);
    }
}

static void draw_desktop(uint32_t mouse_x, uint32_t mouse_y)
{
    draw_wallpaper();
    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        if ((int32_t)index != active_window) {
            draw_window(index);
        }
    }
    if (active_window >= 0) {
        draw_window((uint32_t)active_window);
    }
    draw_start_menu();
    draw_cursor(mouse_x, mouse_y);
}

static void open_window(uint32_t index)
{
    if (index >= WINDOW_COUNT) {
        return;
    }
    struct desktop_window *window = &windows[index];
    window->open = 1u;
    window->minimized = 0u;
    if (!window->maximized) {
        window->x = 72 + (int32_t)index * 8;
        window->y = 28 + (int32_t)index * 7;
        window->width = 192u;
        window->height = 138u;
    }
    active_window = (int32_t)index;
    menu_open = 0u;
}

static void handle_click(uint32_t x, uint32_t y)
{
    if (y >= (uint32_t)TASKBAR_Y && x >= 5u && x < 52u) {
        menu_open ^= 1u;
        return;
    }
    if (menu_open && x < 120u && y >= 125u && y < 178u) {
        open_window((y - 125u) / 15u);
        return;
    }

    if (x < 64u) {
        for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
            uint32_t top = 18u + index * 48u;
            if (y >= top && y < top + 36u) {
                open_window(index);
                return;
            }
        }
    }

    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        struct desktop_window *window = &windows[index];
        if (!window->open || window->minimized ||
            (int32_t)x < window->x || (int32_t)y < window->y ||
            (int32_t)x >= window->x + (int32_t)window->width ||
            (int32_t)y >= window->y + (int32_t)window->height) {
            continue;
        }

        active_window = (int32_t)index;
        int32_t local_x = (int32_t)x - window->x;
        int32_t local_y = (int32_t)y - window->y;
        if (local_y < 15 && local_x >= (int32_t)window->width - 14) {
            window->open = 0u;
            active_window = -1;
        } else if (local_y < 15 && local_x >= (int32_t)window->width - 25) {
            window->maximized ^= 1u;
            if (window->maximized) {
                window->x = 0;
                window->y = 15;
                window->width = VIDEO_WIDTH;
                window->height = 166u;
            } else {
                window->x = 72;
                window->y = 28;
                window->width = 192u;
                window->height = 138u;
            }
        } else if (local_y < 15 &&
                   local_x >= (int32_t)window->width - 36) {
            window->minimized = 1u;
            active_window = -1;
        } else if (local_y < 15) {
            dragging = DRAG_TITLE;
            drag_offset_x = local_x;
            drag_offset_y = local_y;
        } else if (local_x >= (int32_t)window->width - 10 &&
                   local_y >= (int32_t)window->height - 10) {
            dragging = DRAG_RESIZE;
            drag_offset_x = (int32_t)x;
            drag_offset_y = (int32_t)y;
        }
        return;
    }

    if (y >= (uint32_t)TASKBAR_Y) {
        uint32_t index = x >= 57u ? (x - 57u) / 56u : WINDOW_COUNT;
        if (index < WINDOW_COUNT && windows[index].open) {
            windows[index].minimized = 0u;
            active_window = (int32_t)index;
        }
    }
}

static void update_drag(uint32_t x, uint32_t y, uint32_t buttons)
{
    if ((buttons & 1u) == 0u) {
        dragging = DRAG_NONE;
        return;
    }
    if (active_window < 0 || dragging == DRAG_NONE) {
        return;
    }

    struct desktop_window *window = &windows[(uint32_t)active_window];
    if (dragging == DRAG_TITLE) {
        window->x = clamp((int32_t)x - drag_offset_x,
                          0, (int32_t)VIDEO_WIDTH - (int32_t)window->width);
        window->y = clamp((int32_t)y - drag_offset_y,
                          15, TASKBAR_Y - (int32_t)window->height);
    } else {
        window->width = (uint32_t)clamp((int32_t)x - window->x + 1,
                                        (int32_t)WINDOW_MIN_WIDTH,
                                        (int32_t)VIDEO_WIDTH - window->x);
        window->height = (uint32_t)clamp((int32_t)y - window->y + 1,
                                         (int32_t)WINDOW_MIN_HEIGHT,
                                         TASKBAR_Y - window->y);
    }
}

void desktop_run(void)
{
    for (uint32_t index = 0; index < WINDOW_COUNT; ++index) {
        windows[index] = (struct desktop_window){0};
    }
    active_window = -1;
    menu_open = 0u;
    dragging = DRAG_NONE;
    if (!video_set_graphics_mode()) {
        console_write("Unable to set VGA graphics mode.\n");
        return;
    }

    uint8_t previous_buttons = 0u;
    for (;;) {
        struct mouse_state mouse;
        mouse_get_state(&mouse);
        uint32_t mouse_x = mouse.x / 2u;
        uint32_t mouse_y = mouse.y;
        if (mouse_x >= VIDEO_WIDTH) {
            mouse_x = VIDEO_WIDTH - 1u;
        }
        if (mouse_y >= VIDEO_HEIGHT) {
            mouse_y = VIDEO_HEIGHT - 1u;
        }

        char key;
        while (keyboard_read_character(&key)) {
            if ((uint8_t)key == 0x1bu) {
                (void)video_set_text_mode();
                console_clear();
                console_write("Returned from TexugOS Desktop.\n");
                return;
            }
            if (key == '1') {
                open_window(0u);
            } else if (key == '2') {
                open_window(1u);
            } else if (key == '3') {
                open_window(2u);
            }
        }

        if ((mouse.buttons & 1u) != 0u &&
            (previous_buttons & 1u) == 0u) {
            handle_click(mouse_x, mouse_y);
        }
        update_drag(mouse_x, mouse_y, mouse.buttons);
        previous_buttons = mouse.buttons;
        draw_desktop(mouse_x, mouse_y);
        __asm__ volatile ("hlt");
    }
}
