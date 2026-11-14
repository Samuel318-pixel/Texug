#include <stdint.h>

#include "ata.h"

#define ATA_DATA 0x1f0u
#define ATA_SECTOR_COUNT 0x1f2u
#define ATA_LBA_LOW 0x1f3u
#define ATA_LBA_MID 0x1f4u
#define ATA_LBA_HIGH 0x1f5u
#define ATA_DRIVE 0x1f6u
#define ATA_STATUS 0x1f7u
#define ATA_COMMAND 0x1f7u
#define ATA_ALT_STATUS 0x3f6u
#define ATA_STATUS_ERROR 0x01u
#define ATA_STATUS_DEVICE_FAULT 0x20u
#define ATA_STATUS_DRQ 0x08u
#define ATA_STATUS_BUSY 0x80u
#define ATA_TIMEOUT 1000000u

static int disk_present;
static uint32_t sector_count;
static char disk_model[41];

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

static void out16(uint16_t port, uint16_t value)
{
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static uint16_t in16(uint16_t port)
{
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void io_delay(void)
{
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
    (void)in8(ATA_ALT_STATUS);
}

static int wait_not_busy(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        return 1;
    }
    return 0;
}

static int wait_for_data(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        if ((status & ATA_STATUS_DRQ) != 0u) {
            return 1;
        }
    }
    return 0;
}

static int wait_complete(void)
{
    for (uint32_t count = 0; count < ATA_TIMEOUT; ++count) {
        uint8_t status = in8(ATA_STATUS);
        if (status == 0u || status == 0xffu) {
            return 0;
        }
        if ((status & ATA_STATUS_BUSY) != 0u) {
            continue;
        }
        if ((status & (ATA_STATUS_ERROR | ATA_STATUS_DEVICE_FAULT)) != 0u) {
            return 0;
        }
        if ((status & ATA_STATUS_DRQ) == 0u) {
            return 1;
        }
    }
    return 0;
}

int ata_initialize(void)
{
    uint16_t identify[256];
    out8(ATA_DRIVE, 0xa0u);
    io_delay();
    out8(ATA_SECTOR_COUNT, 0u);
    out8(ATA_LBA_LOW, 0u);
    out8(ATA_LBA_MID, 0u);
    out8(ATA_LBA_HIGH, 0u);
    out8(ATA_COMMAND, 0xecu);

    uint8_t status = in8(ATA_STATUS);
    if (status == 0u || status == 0xffu) {
        return 0;
    }
    if (!wait_not_busy() || in8(ATA_LBA_MID) != 0u ||
        in8(ATA_LBA_HIGH) != 0u || !wait_for_data()) {
        return 0;
    }

    for (uint32_t index = 0; index < 256u; ++index) {
        identify[index] = in16(ATA_DATA);
    }
    if ((identify[49] & 0x0200u) == 0u) {
        return 0;
    }

    sector_count = (uint32_t)identify[60] |
                   ((uint32_t)identify[61] << 16);
    if (sector_count == 0u) {
        return 0;
    }

    for (uint32_t index = 0; index < 20u; ++index) {
        disk_model[index * 2u] = (char)(identify[27u + index] >> 8);
        disk_model[index * 2u + 1u] = (char)identify[27u + index];
    }
    disk_model[40] = '\0';
    for (int32_t end = 39; end >= 0 && disk_model[end] == ' '; --end) {
        disk_model[end] = '\0';
    }

    disk_present = 1;
    return 1;
}

const char *ata_model(void)
{
    return disk_model;
}

static int select_sector(uint32_t lba, uint8_t command)
{
    if (!disk_present || lba >= sector_count || lba >= 0x10000000u) {
        return 0;
    }
    if (!wait_complete()) {
        return 0;
    }

    out8(ATA_DRIVE, (uint8_t)(0xe0u | ((lba >> 24) & 0x0fu)));
    io_delay();
    out8(ATA_SECTOR_COUNT, 1u);
    out8(ATA_LBA_LOW, (uint8_t)lba);
    out8(ATA_LBA_MID, (uint8_t)(lba >> 8));
    out8(ATA_LBA_HIGH, (uint8_t)(lba >> 16));
    out8(ATA_COMMAND, command);
    if (!wait_for_data()) {
        return 0;
    }
    return 1;
}

int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    if (buffer == 0 || !select_sector(lba, 0x20u)) {
        return 0;
    }

    uint16_t *words = (uint16_t *)buffer;
    for (uint32_t index = 0; index < 256u; ++index) {
        words[index] = in16(ATA_DATA);
    }
    return 1;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    if (buffer == 0 || !select_sector(lba, 0x30u)) {
        return 0;
    }

    const uint16_t *words = (const uint16_t *)buffer;
    for (uint32_t index = 0; index < 256u; ++index) {
        out16(ATA_DATA, words[index]);
    }
    if (!wait_complete()) {
        return 0;
    }

    return 1;
}
