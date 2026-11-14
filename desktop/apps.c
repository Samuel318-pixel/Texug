#include <stdint.h>

#include "apps.h"
#include "arena.h"
#include "fat32.h"
#include "frame_allocator.h"
#include "mouse.h"
#include "network.h"
#include "pci.h"
#include "pit.h"
#include "video.h"

#define APP_COUNT 5u
#define FILE_CAPACITY 64u
#define FILE_PREVIEW_SIZE 256u
#define TERMINAL_ROWS 6u
#define TERMINAL_LINE_SIZE 28u
#define TERMINAL_INPUT_SIZE 40u
#define BROWSER_RESPONSE_SIZE 768u
#define BROWSER_LINE_COUNT 6u
#define BROWSER_LINE_SIZE 28u
#define BROWSER_INPUT_SIZE 64u

static uint32_t files_directory;
static uint32_t files_selected;
static uint8_t files_preview_open;
static char files_preview[FILE_PREVIEW_SIZE + 1u];
static uint32_t files_preview_length;
static struct fat32_entry file_entries[FILE_CAPACITY];
static uint32_t file_entry_count;
static uint8_t file_list_valid;
static uint8_t file_list_dirty;

static char terminal_lines[TERMINAL_ROWS][TERMINAL_LINE_SIZE];
static uint32_t terminal_line_count;
static char terminal_input[TERMINAL_INPUT_SIZE];
static uint32_t terminal_input_length;
static struct fat32_entry terminal_entries[FILE_CAPACITY];
static char browser_input[BROWSER_INPUT_SIZE];
static uint32_t browser_input_length;
static char browser_lines[BROWSER_LINE_COUNT][BROWSER_LINE_SIZE];
static uint32_t browser_line_count;
static char browser_status[BROWSER_LINE_SIZE];
static uint8_t browser_status_color;

static uint32_t text_length(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

static void text_copy(char *destination, const char *source,
                      uint32_t capacity)
{
    if (capacity == 0u) {
        return;
    }
    uint32_t index = 0u;
    while (index + 1u < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

static void append_u32(char *buffer, uint32_t value)
{
    char digits[10];
    uint32_t count = 0u;
    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    uint32_t length = text_length(buffer);
    while (count != 0u && length + 1u < TERMINAL_LINE_SIZE) {
        buffer[length++] = digits[--count];
    }
    buffer[length] = '\0';
}

static int text_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static int text_starts_with(const char *text, const char *prefix)
{
    while (*prefix != '\0') {
        if (*text++ != *prefix++) {
            return 0;
        }
    }
    return 1;
}

static void terminal_clear_lines(void)
{
    terminal_line_count = 0u;
    for (uint32_t row = 0; row < TERMINAL_ROWS; ++row) {
        terminal_lines[row][0] = '\0';
    }
}

static void terminal_add_line(const char *text)
{
    if (terminal_line_count == TERMINAL_ROWS) {
        for (uint32_t row = 1u; row < TERMINAL_ROWS; ++row) {
            text_copy(terminal_lines[row - 1u], terminal_lines[row],
                      TERMINAL_LINE_SIZE);
        }
        terminal_line_count = TERMINAL_ROWS - 1u;
    }
    text_copy(terminal_lines[terminal_line_count], text,
              TERMINAL_LINE_SIZE);
    ++terminal_line_count;
}

static void terminal_add_entry_list(void)
{
    uint32_t count = 0u;
    if (!fat32_list(fat32_root_cluster(), terminal_entries, FILE_CAPACITY,
                    &count)) {
        terminal_add_line("DIRECTORY LIST FAILED");
        return;
    }
    if (count == 0u) {
        terminal_add_line("EMPTY");
        return;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        terminal_add_line(terminal_entries[index].name);
    }
}

static void terminal_add_file(const char *name)
{
    char contents[FILE_PREVIEW_SIZE + 1u];
    uint32_t size = 0u;
    if (!fat32_read_file(fat32_root_cluster(), name, contents,
                         FILE_PREVIEW_SIZE, &size)) {
        terminal_add_line("FILE READ FAILED");
        return;
    }
    contents[size] = '\0';
    if (size == 0u) {
        terminal_add_line("EMPTY FILE");
        return;
    }

    char line[TERMINAL_LINE_SIZE];
    uint32_t length = 0u;
    for (uint32_t index = 0u; index < size; ++index) {
        char character = contents[index];
        if (character == '\n' || length + 1u == TERMINAL_LINE_SIZE) {
            line[length] = '\0';
            terminal_add_line(line);
            length = 0u;
            if (character == '\n') {
                continue;
            }
        }
        if (character >= ' ' && character <= '~') {
            line[length++] = character;
        }
    }
    if (length != 0u) {
        line[length] = '\0';
        terminal_add_line(line);
    }
}

static void terminal_execute(void)
{
    char command_line[TERMINAL_INPUT_SIZE];
    char echo_line[TERMINAL_LINE_SIZE];
    text_copy(command_line, terminal_input, sizeof(command_line));
    echo_line[0] = '>';
    echo_line[1] = ' ';
    text_copy(&echo_line[2], command_line, sizeof(echo_line) - 2u);
    terminal_add_line(echo_line);

    if (text_equal(command_line, "help")) {
        terminal_add_line("HELP LS CAT ECHO");
        terminal_add_line("TOUCH MKDIR RM");
    } else if (text_equal(command_line, "ls")) {
        terminal_add_entry_list();
    } else if (text_equal(command_line, "memory")) {
        char line[TERMINAL_LINE_SIZE] = "FREE FRAMES ";
        append_u32(line, frame_free_count());
        terminal_add_line(line);
    } else if (text_equal(command_line, "version")) {
        terminal_add_line("TEXUGOS VERSION 0.1.0");
    } else if (text_equal(command_line, "clear")) {
        terminal_clear_lines();
    } else if (text_starts_with(command_line, "echo ")) {
        terminal_add_line(command_line + 5);
    } else if (text_starts_with(command_line, "cat ")) {
        terminal_add_file(command_line + 4);
    } else if (text_starts_with(command_line, "touch ")) {
        if (fat32_write_file(fat32_root_cluster(), command_line + 6,
                             "", 0u)) {
            file_list_dirty = 1u;
            terminal_add_line("FILE CREATED");
        } else {
            terminal_add_line("FILE CREATE FAILED");
        }
    } else if (text_starts_with(command_line, "mkdir ")) {
        uint32_t cluster;
        if (fat32_create_directory(fat32_root_cluster(), command_line + 6,
                                   &cluster)) {
            file_list_dirty = 1u;
            terminal_add_line("DIRECTORY CREATED");
        } else {
            terminal_add_line("DIRECTORY CREATE FAILED");
        }
    } else if (text_starts_with(command_line, "rm ")) {
        if (fat32_delete(fat32_root_cluster(), command_line + 3)) {
            file_list_dirty = 1u;
            terminal_add_line("ENTRY REMOVED");
        } else {
            terminal_add_line("REMOVE FAILED");
        }
    } else if (*command_line != '\0') {
        terminal_add_line("UNKNOWN COMMAND");
    }
    terminal_input_length = 0u;
    terminal_input[0] = '\0';
}

static int parse_browser_target(const char *text, char host[64],
                                uint16_t *port, char path[96])
{
    const char *cursor = text;
    static const char http_prefix[] = "http://";
    if (text_starts_with(cursor, http_prefix)) {
        cursor += sizeof(http_prefix) - 1u;
    }
    uint32_t host_length = 0u;
    while (*cursor != '\0' && *cursor != ':' && *cursor != '/') {
        char character = *cursor++;
        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '-') ||
            host_length + 1u >= 64u) {
            return 0;
        }
        host[host_length++] = character;
    }
    if (host_length == 0u) {
        return 0;
    }
    host[host_length] = '\0';

    *port = 80u;
    if (*cursor == ':') {
        ++cursor;
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10u + (uint32_t)(*cursor++ - '0');
            if (value > 65535u || ++digits > 5u) {
                return 0;
            }
        }
        if (digits == 0u || value == 0u) {
            return 0;
        }
        *port = (uint16_t)value;
    }

    if (*cursor == '\0') {
        path[0] = '/';
        path[1] = '\0';
        return 1;
    }
    if (*cursor != '/') {
        return 0;
    }
    text_copy(path, cursor, 96u);
    return text_length(path) == text_length(cursor);
}

static int parse_browser_ipv4(const char *host, uint8_t address[4])
{
    const char *cursor = host;
    for (uint32_t part = 0u; part < 4u; ++part) {
        uint32_t value = 0u;
        uint32_t digits = 0u;
        while (*cursor >= '0' && *cursor <= '9') {
            value = value * 10u + (uint32_t)(*cursor++ - '0');
            if (value > 255u || ++digits > 3u) {
                return 0;
            }
        }
        if (digits == 0u || (part < 3u ? *cursor++ != '.' : *cursor != '\0')) {
            return 0;
        }
        address[part] = (uint8_t)value;
    }
    return 1;
}

static void browser_add_character(char character, uint32_t *line_length,
                                  char line[BROWSER_LINE_SIZE])
{
    if (*line_length + 1u >= BROWSER_LINE_SIZE ||
        browser_line_count == BROWSER_LINE_COUNT) {
        return;
    }
    line[(*line_length)++] = character;
}

static void browser_flush_line(char line[BROWSER_LINE_SIZE],
                               uint32_t *line_length)
{
    if (browser_line_count == BROWSER_LINE_COUNT) {
        *line_length = 0u;
        return;
    }
    line[*line_length] = '\0';
    text_copy(browser_lines[browser_line_count++], line,
              BROWSER_LINE_SIZE);
    *line_length = 0u;
}

static int browser_render_response(const char *response, uint32_t length)
{
    uint32_t body = 0u;
    while (body + 3u < length) {
        if (response[body] == '\r' && response[body + 1u] == '\n' &&
            response[body + 2u] == '\r' && response[body + 3u] == '\n') {
            body += 4u;
            break;
        }
        ++body;
    }
    if (body >= length) {
        return 0;
    }

    uint8_t inside_tag = 0u;
    char line[BROWSER_LINE_SIZE];
    uint32_t line_length = 0u;
    for (uint32_t index = body; index < length; ++index) {
        char character = response[index];
        if (character == '<') {
            inside_tag = 1u;
            continue;
        }
        if (character == '>') {
            inside_tag = 0u;
            browser_add_character(' ', &line_length, line);
            continue;
        }
        if (inside_tag) {
            continue;
        }
        if (character == '\r') {
            continue;
        }
        if (character == '\n') {
            browser_flush_line(line, &line_length);
            if (browser_line_count == BROWSER_LINE_COUNT) {
                break;
            }
        } else if (character == ' ' || character == '\t') {
            if (line_length != 0u &&
                line[line_length - 1u] != ' ') {
                browser_add_character(' ', &line_length, line);
            }
        } else if (character >= ' ' && character <= '~') {
            browser_add_character(character, &line_length, line);
        }
    }
    if (line_length != 0u && browser_line_count < BROWSER_LINE_COUNT) {
        browser_flush_line(line, &line_length);
    }
    return 1;
}

static void browser_load_page(void)
{
    uint8_t address[4];
    uint16_t port;
    char host[64];
    char path[96];
    browser_line_count = 0u;
    if (!parse_browser_target(browser_input, host, &port, path)) {
        text_copy(browser_status, "INVALID HTTP URL", sizeof(browser_status));
        browser_status_color = 4u;
        return;
    }
    struct network_config config;
    network_get_config(&config);
    if (config.configured == 0u) {
        text_copy(browser_status, "RUN DHCP FIRST", sizeof(browser_status));
        browser_status_color = 14u;
        return;
    }
    if (!parse_browser_ipv4(host, address) &&
        !network_resolve_ipv4(host, address)) {
        text_copy(browser_status, "DNS LOOKUP FAILED",
                  sizeof(browser_status));
        browser_status_color = 12u;
        return;
    }

    char response[BROWSER_RESPONSE_SIZE];
    uint32_t response_length = 0u;
    if (!network_http_get(address, port, host, path, response,
                          sizeof(response), &response_length)) {
        text_copy(browser_status, "HTTP REQUEST FAILED",
                  sizeof(browser_status));
        browser_status_color = 5u;
        return;
    }
    if (!browser_render_response(response, response_length)) {
        text_copy(browser_status, "INVALID HTTP RESPONSE",
                  sizeof(browser_status));
        browser_status_color = 4u;
        return;
    }
    text_copy(browser_status, "HTTP RESPONSE RECEIVED",
              sizeof(browser_status));
    browser_status_color = 10u;
}

static void draw_terminal(int32_t x, int32_t y)
{
    video_draw_text(x + 5, y + 5, "TEXUGOS TERMINAL", 1u);
    for (uint32_t row = 0u; row < terminal_line_count; ++row) {
        video_draw_text(x + 5, y + 17 + (int32_t)row * 11,
                        terminal_lines[row], 1u);
    }
    video_draw_text(x + 5, y + 87, ">", 1u);
    uint32_t input_start = terminal_input_length > 27u
        ? terminal_input_length - 27u : 0u;
    video_draw_text(x + 17, y + 87, terminal_input + input_start, 1u);
}

static void draw_browser(int32_t x, int32_t y)
{
    video_draw_text(x + 5, y + 5, "HTTP BROWSER URL", 1u);
    video_draw_text(x + 5, y + 16, browser_status, browser_status_color);
    for (uint32_t row = 0u; row < browser_line_count; ++row) {
        video_draw_text(x + 5, y + 28 + (int32_t)row * 10,
                        browser_lines[row], 1u);
    }
    video_draw_text(x + 5, y + 87, ">", 1u);
    uint32_t input_start = browser_input_length > 27u
        ? browser_input_length - 27u : 0u;
    video_draw_text(x + 17, y + 87, browser_input + input_start, 1u);
}

static void refresh_file_list(void)
{
    file_list_valid = (uint8_t)fat32_list(files_directory, file_entries,
                                          FILE_CAPACITY, &file_entry_count);
    file_list_dirty = 0u;
    if (files_selected >= file_entry_count) {
        files_selected = 0u;
    }
}

static void draw_files(int32_t x, int32_t y, uint32_t height)
{
    if (files_preview_open) {
        video_draw_text(x + 5, y + 5, "FILE PREVIEW", 1u);
        if (files_preview_length == 0u) {
            video_draw_text(x + 5, y + 18, "EMPTY FILE", 1u);
        } else {
            char line[27];
            uint32_t line_length = 0u;
            int32_t line_y = y + 18;
            for (uint32_t index = 0u; index < files_preview_length &&
                 line_y + 7 < y + (int32_t)height; ++index) {
                char character = files_preview[index];
                if (character == '\n' || line_length == sizeof(line) - 1u) {
                    line[line_length] = '\0';
                    video_draw_text(x + 5, line_y, line, 1u);
                    line_y += 10;
                    line_length = 0u;
                    if (character == '\n') {
                        continue;
                    }
                }
                if (character >= ' ' && character <= '~') {
                    line[line_length++] = character;
                }
            }
            if (line_length != 0u && line_y + 7 < y + (int32_t)height) {
                line[line_length] = '\0';
                video_draw_text(x + 5, line_y, line, 1u);
            }
        }
        video_draw_text(x + 5, y + (int32_t)height - 12, "BACKSPACE BACK",
                        1u);
        return;
    }

    if (file_list_dirty != 0u) {
        refresh_file_list();
    }
    video_draw_text(x + 5, y + 5, "ROOT FILES ENTER OPEN", 1u);
    if (!file_list_valid) {
        video_draw_text(x + 5, y + 18, "DIRECTORY LIST FAILED", 4u);
        return;
    }
    if (file_entry_count == 0u) {
        video_draw_text(x + 5, y + 18, "EMPTY DIRECTORY", 1u);
        return;
    }

    uint32_t visible = (height - 18u) / 10u;
    if (visible > file_entry_count) {
        visible = file_entry_count;
    }
    for (uint32_t row = 0u; row < visible; ++row) {
        uint32_t index = row;
        if (index == files_selected) {
            video_fill_rect(x + 3, y + 16 + (int32_t)row * 10,
                            176u, 10u, 9u);
        }
        video_draw_text(x + 5, y + 18 + (int32_t)row * 10,
                        file_entries[index].name,
                        index == files_selected ? 15u : 1u);
        if ((file_entries[index].attributes &
             FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
            video_draw_text(x + 5 + (int32_t)text_length(
                                file_entries[index].name) * 6,
                            y + 18 + (int32_t)row * 10, "/", 1u);
        }
    }
}

static void draw_settings(int32_t x, int32_t y)
{
    struct mouse_state mouse;
    mouse_get_state(&mouse);

    video_draw_text(x + 5, y + 5, "SYSTEM INFORMATION", 1u);
    video_draw_text(x + 5, y + 18, "DISPLAY 320 X 200", 1u);
    video_draw_text(x + 5, y + 29, "BOOT LEGACY BIOS", 1u);
    video_draw_text(x + 5, y + 40, "FREE FRAMES", 1u);
    char value[TERMINAL_LINE_SIZE] = "";
    append_u32(value, frame_free_count());
    video_draw_text(x + 88, y + 40, value, 1u);
    video_draw_text(x + 5, y + 51, "PCI DEVICES", 1u);
    value[0] = '\0';
    append_u32(value, pci_device_count());
    video_draw_text(x + 88, y + 51, value, 1u);
    video_draw_text(x + 5, y + 62, "MOUSE", 1u);
    value[0] = '\0';
    append_u32(value, mouse.x);
    video_draw_text(x + 48, y + 62, value, 1u);
    video_draw_text(x + 5, y + 73, "TIMER TICKS", 1u);
    value[0] = '\0';
    append_u32(value, pit_ticks_read());
    video_draw_text(x + 72, y + 73, value, 1u);
}

void desktop_apps_initialize(void)
{
    files_directory = fat32_root_cluster();
    files_selected = 0u;
    files_preview_open = 0u;
    files_preview_length = 0u;
    file_entry_count = 0u;
    file_list_valid = 1u;
    file_list_dirty = 1u;
    terminal_input_length = 0u;
    terminal_input[0] = '\0';
    terminal_clear_lines();
    terminal_add_line("TEXUGOS TERMINAL READY");
    terminal_add_line("TYPE HELP FOR COMMANDS");
    arena_initialize();
    browser_input_length = 0u;
    browser_input[0] = '\0';
    browser_line_count = 0u;
    text_copy(browser_status, "ENTER HOST:PORT/PATH",
              sizeof(browser_status));
    browser_status_color = 4u;
}

void desktop_app_draw(uint32_t index, int32_t x, int32_t y,
                      uint32_t width, uint32_t height)
{
    if (index >= APP_COUNT) {
        return;
    }
    if (index == 0u) {
        draw_files(x, y, height);
    } else if (index == 1u) {
        draw_terminal(x, y);
    } else if (index == 2u) {
        draw_settings(x, y);
    } else if (index == 3u) {
        draw_browser(x, y);
    } else {
        arena_draw(x, y, width, height);
    }
}

void desktop_app_handle_key(uint32_t index, char key)
{
    if (index == 4u) {
        arena_handle_key(key);
        return;
    }
    if (index == 0u) {
        if (key == '\b') {
            if (files_preview_open) {
                files_preview_open = 0u;
            } else if (files_directory != fat32_root_cluster()) {
                struct fat32_entry parent;
                if (fat32_find(files_directory, "..", &parent)) {
                    files_directory = parent.first_cluster;
                    files_selected = 0u;
                    file_list_dirty = 1u;
                }
            }
        } else if (key == '\n' && !files_preview_open) {
            if (file_list_dirty != 0u) {
                refresh_file_list();
            }
            if (file_list_valid && files_selected < file_entry_count) {
                struct fat32_entry *entry = &file_entries[files_selected];
                if ((entry->attributes & FAT32_ATTRIBUTE_DIRECTORY) != 0u) {
                    files_directory = entry->first_cluster;
                    files_selected = 0u;
                    file_list_dirty = 1u;
                } else if (fat32_read_file(files_directory, entry->name,
                                           files_preview,
                                           FILE_PREVIEW_SIZE,
                                           &files_preview_length)) {
                    files_preview[files_preview_length] = '\0';
                    files_preview_open = 1u;
                }
            }
        }
        return;
    }

    if (index == 3u) {
        if (key == '\n') {
            browser_load_page();
        } else if (key == '\b') {
            if (browser_input_length != 0u) {
                browser_input[--browser_input_length] = '\0';
            }
        } else if (key >= ' ' && key <= '~' &&
                   browser_input_length + 1u < BROWSER_INPUT_SIZE) {
            browser_input[browser_input_length++] = key;
            browser_input[browser_input_length] = '\0';
        }
        return;
    }
    if (index != 1u) {
        return;
    }
    if (key == '\n') {
        terminal_execute();
    } else if (key == '\b') {
        if (terminal_input_length != 0u) {
            terminal_input[--terminal_input_length] = '\0';
        }
    } else if (key >= ' ' && key <= '~' &&
               terminal_input_length + 1u < TERMINAL_INPUT_SIZE) {
        terminal_input[terminal_input_length++] = key;
        terminal_input[terminal_input_length] = '\0';
    }
}

void desktop_app_handle_mouse(uint32_t index, int32_t delta_x)
{
    if (index == 4u) {
        arena_handle_mouse(delta_x);
    }
}

void desktop_app_handle_click(uint32_t index, int32_t x, int32_t y)
{
    if (index == 4u) {
        arena_fire();
        return;
    }
    if (index != 0u || files_preview_open || x < 0 || y < 22) {
        return;
    }
    if (file_list_dirty != 0u) {
        refresh_file_list();
    }
    if (!file_list_valid) {
        return;
    }
    uint32_t selected = (uint32_t)(y - 22) / 10u;
    if (selected < file_entry_count) {
        files_selected = selected;
    }
}
