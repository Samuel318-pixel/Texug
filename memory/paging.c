#include <stdint.h>

#include "paging.h"

#define PAGE_SIZE 4096u
#define PAGE_ENTRIES 1024u
#define IDENTITY_TABLES 4u
#define PAGE_PRESENT_WRITE 0x003u
#define PAGE_USER 0x004u
#define IDENTITY_MAP_SIZE (IDENTITY_TABLES * PAGE_ENTRIES * PAGE_SIZE)

static uint32_t page_directory[PAGE_ENTRIES] __attribute__((aligned(PAGE_SIZE)));
static uint32_t page_tables[IDENTITY_TABLES][PAGE_ENTRIES]
    __attribute__((aligned(PAGE_SIZE)));

void paging_initialize(void)
{
    for (uint32_t index = 0; index < PAGE_ENTRIES; ++index) {
        page_directory[index] = 0;
    }

    for (uint32_t table = 0; table < IDENTITY_TABLES; ++table) {
        page_directory[table] =
            (uint32_t)(uintptr_t)page_tables[table] | PAGE_PRESENT_WRITE;
        for (uint32_t page = 0; page < PAGE_ENTRIES; ++page) {
            uint32_t physical_address =
                (table * PAGE_ENTRIES + page) * PAGE_SIZE;
            page_tables[table][page] = physical_address | PAGE_PRESENT_WRITE;
        }
    }

    uint32_t directory_address = (uint32_t)(uintptr_t)page_directory;
    uint32_t cr0;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(directory_address) : "memory");
    __asm__ volatile ("movl %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile ("movl %0, %%cr0" : : "r"(cr0) : "memory");
}

int paging_set_user_page(uint32_t address, int user, int writable)
{
    if (address >= IDENTITY_MAP_SIZE ||
        (address & (PAGE_SIZE - 1u)) != 0u) {
        return 0;
    }

    uint32_t directory_index = address >> 22;
    uint32_t table_index = (address >> 12) & (PAGE_ENTRIES - 1u);
    uint32_t *entry = &page_tables[directory_index][table_index];
    if (user) {
        page_directory[directory_index] |= PAGE_USER;
        *entry |= PAGE_USER;
        if (writable) {
            *entry |= 0x002u;
        } else {
            *entry &= ~0x002u;
        }
    } else {
        *entry = (address & ~(PAGE_SIZE - 1u)) | PAGE_PRESENT_WRITE;
        if (page_directory[directory_index] & PAGE_USER) {
            int has_user_page = 0;
            for (uint32_t index = 0u; index < PAGE_ENTRIES; ++index) {
                if ((page_tables[directory_index][index] & PAGE_USER) != 0u) {
                    has_user_page = 1;
                    break;
                }
            }
            if (!has_user_page) {
                page_directory[directory_index] &= ~PAGE_USER;
            }
        }
    }

    uint32_t directory_address = (uint32_t)(uintptr_t)page_directory;
    __asm__ volatile ("movl %0, %%cr3" : : "r"(directory_address) : "memory");
    return 1;
}

uint32_t paging_fault_address(void)
{
    uint32_t address;
    __asm__ volatile ("movl %%cr2, %0" : "=r"(address));
    return address;
}
