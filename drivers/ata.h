#ifndef TEXUGOS_ATA_H
#define TEXUGOS_ATA_H

#include <stdint.h>

int ata_initialize(void);
const char *ata_model(void);
uint32_t ata_sector_count(void);
int ata_read_sector(uint32_t lba, uint8_t *buffer);
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
