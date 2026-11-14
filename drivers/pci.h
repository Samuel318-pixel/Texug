#ifndef TEXUGOS_PCI_H
#define TEXUGOS_PCI_H

#include <stdint.h>

struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t programming_interface;
    uint16_t vendor_id;
    uint16_t device_id;
};

void pci_initialize(void);
uint32_t pci_device_count(void);
int pci_inventory_truncated(void);
int pci_get_device(uint32_t index, struct pci_device *device);

#endif
