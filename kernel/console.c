#include <stdint.h>

#include "console.h"

#define VGA_MEMORY ((volatile uint16_t *)0xb8000u)
#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_COLOR 0x0fu

static uint32_t cursor;

static void put_character(char character)
{
    if (character == '\n') {
        cursor = ((cursor / VGA_WIDTH) + 1u) * VGA_WIDTH;
        return;
    }

    if (cursor >= VGA_WIDTH * VGA_HEIGHT) {
        cursor = 0;
    }
    VGA_MEMORY[cursor++] = (uint16_t)((VGA_COLOR << 8) | (uint8_t)character);
}

void console_clear(void)
{
    for (uint32_t index = 0; index < VGA_WIDTH * VGA_HEIGHT; ++index) {
        VGA_MEMORY[index] = (uint16_t)((VGA_COLOR << 8) | ' ');
    }
    cursor = 0;
}

void console_write(const char *text)
{
    while (*text != '\0') {
        put_character(*text++);
    }
}

void console_write_u32(uint32_t value)
{
    char digits[10];
    uint32_t length = 0;

    do {
        digits[length++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    while (length != 0u) {
        put_character(digits[--length]);
    }
}
