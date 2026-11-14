#include <stdint.h>

#include "paging.h"

#define PAGE_SIZE 4096u
#define PAGE_ENTRIES 1024u
#define IDENTITY_TABLES 4u
#define PAGE_PRESENT_WRITE 0x003u

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
