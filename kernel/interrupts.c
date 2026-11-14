#include <stdint.h>

#include "console.h"
#include "interrupts.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "mouse.h"
#include "threads.h"

#define IDT_ENTRIES 48u
#define KERNEL_CODE_SELECTOR 0x08u
#define INTERRUPT_GATE 0x8eu

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t reserved;
    uint8_t flags;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct interrupt_frame {
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t saved_esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t vector;
    uint32_t error_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
};

static struct idt_entry idt[IDT_ENTRIES];
extern const uint32_t interrupt_stub_table[IDT_ENTRIES];

static void idt_set_gate(uint32_t vector, uint32_t handler)
{
    idt[vector].offset_low = (uint16_t)(handler & 0xffffu);
    idt[vector].selector = KERNEL_CODE_SELECTOR;
    idt[vector].reserved = 0;
    idt[vector].flags = INTERRUPT_GATE;
    idt[vector].offset_high = (uint16_t)(handler >> 16);
}

void interrupts_initialize(void)
{
    for (uint32_t vector = 0; vector < IDT_ENTRIES; ++vector) {
        idt_set_gate(vector, interrupt_stub_table[vector]);
    }

    const struct idt_pointer pointer = {
        .limit = (uint16_t)(sizeof(idt) - 1u),
        .base = (uint32_t)(uintptr_t)idt,
    };
    __asm__ volatile ("lidt %0" : : "m"(pointer));
}

void interrupt_dispatch(const uint32_t *raw_frame)
{
    const struct interrupt_frame *frame =
        (const struct interrupt_frame *)raw_frame;

    if (frame->vector == 32u) {
        pit_handle_interrupt();
        pic_send_eoi(0u);
        thread_preempt();
        return;
    }
    if (frame->vector == 33u) {
        keyboard_handle_interrupt();
        pic_send_eoi(1u);
        return;
    }
    if (frame->vector == 44u) {
        mouse_handle_interrupt();
        pic_send_eoi(12u);
        return;
    }

    if (frame->vector >= 32u && frame->vector < IDT_ENTRIES) {
        pic_send_eoi(frame->vector - 32u);
        return;
    }

    __asm__ volatile ("cli");
    console_write("CPU exception vector ");
    console_write_u32(frame->vector);
    console_write(", error ");
    console_write_u32(frame->error_code);
    console_write("\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
