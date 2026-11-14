#include <stdint.h>

#include "pic.h"

#define PIC_MASTER_COMMAND 0x20u
#define PIC_MASTER_DATA 0x21u
#define PIC_SLAVE_COMMAND 0xa0u
#define PIC_SLAVE_DATA 0xa1u

static void out8(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void io_wait(void)
{
    out8(0x80u, 0u);
}

void pic_initialize(void)
{
    uint8_t master_mask, slave_mask;
    __asm__ volatile ("inb %1, %0" : "=a"(master_mask) : "Nd"(PIC_MASTER_DATA));
    __asm__ volatile ("inb %1, %0" : "=a"(slave_mask) : "Nd"(PIC_SLAVE_DATA));

    out8(PIC_MASTER_COMMAND, 0x11u);
    io_wait();
    out8(PIC_SLAVE_COMMAND, 0x11u);
    io_wait();
    out8(PIC_MASTER_DATA, 0x20u);
    io_wait();
    out8(PIC_SLAVE_DATA, 0x28u);
    io_wait();
    out8(PIC_MASTER_DATA, 0x04u);
    io_wait();
    out8(PIC_SLAVE_DATA, 0x02u);
    io_wait();
    out8(PIC_MASTER_DATA, 0x01u);
    io_wait();
    out8(PIC_SLAVE_DATA, 0x01u);
    io_wait();

    (void)master_mask;
    (void)slave_mask;
    out8(PIC_MASTER_DATA, 0xfeu);
    out8(PIC_SLAVE_DATA, 0xffu);
}

void pic_send_eoi(uint32_t irq)
{
    if (irq >= 8u) {
        out8(PIC_SLAVE_COMMAND, 0x20u);
    }
    out8(PIC_MASTER_COMMAND, 0x20u);
}
