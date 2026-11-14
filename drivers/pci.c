#include <stdint.h>

#include "pci.h"

#define PCI_CONFIG_ADDRESS 0x0cf8u
#define PCI_CONFIG_DATA 0x0cfcu
#define PCI_MAX_DEVICES 64u
#define PCI_FUNCTIONS_PER_SLOT 8u

static struct pci_device devices[PCI_MAX_DEVICES];
static uint32_t device_count;
static int inventory_truncated;

static void out32(uint16_t port, uint32_t value)
{
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static uint32_t in32(uint16_t port)
{
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t read_config(uint8_t bus, uint8_t slot, uint8_t function,
                            uint8_t offset)
{
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) | (offset & 0xfcu);
    out32(PCI_CONFIG_ADDRESS, address);
    return in32(PCI_CONFIG_DATA);
}

void pci_initialize(void)
{
    device_count = 0u;
    inventory_truncated = 0;
    for (uint32_t bus = 0; bus < 256u; ++bus) {
        for (uint32_t slot = 0; slot < 32u; ++slot) {
            uint32_t identity = read_config((uint8_t)bus, (uint8_t)slot, 0u, 0u);
            if ((identity & 0xffffu) == 0xffffu) {
                continue;
            }

            uint32_t header = read_config((uint8_t)bus, (uint8_t)slot,
                                          0u, 0x0cu);
            uint32_t function_count =
                (header & 0x00800000u) != 0u ? PCI_FUNCTIONS_PER_SLOT : 1u;
            for (uint32_t function = 0; function < function_count; ++function) {
                identity = read_config((uint8_t)bus, (uint8_t)slot,
                                       (uint8_t)function, 0u);
                uint16_t vendor = (uint16_t)identity;
                if (vendor == 0xffffu) {
                    continue;
                }
                if (device_count == PCI_MAX_DEVICES) {
                    inventory_truncated = 1;
                    continue;
                }

                uint32_t class_info =
                    read_config((uint8_t)bus, (uint8_t)slot,
                                (uint8_t)function, 0x08u);
                devices[device_count++] = (struct pci_device) {
                    .bus = (uint8_t)bus,
                    .slot = (uint8_t)slot,
                    .function = (uint8_t)function,
                    .class_code = (uint8_t)(class_info >> 24),
                    .subclass = (uint8_t)(class_info >> 16),
                    .programming_interface = (uint8_t)(class_info >> 8),
                    .vendor_id = vendor,
                    .device_id = (uint16_t)(identity >> 16),
                };
            }
        }
    }
}

uint32_t pci_device_count(void)
{
    return device_count;
}

int pci_inventory_truncated(void)
{
    return inventory_truncated;
}

int pci_get_device(uint32_t index, struct pci_device *device)
{
    if (device == 0 || index >= device_count) {
        return 0;
    }
    *device = devices[index];
    return 1;
}
