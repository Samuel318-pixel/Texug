#ifndef TEXUGOS_PAGING_H
#define TEXUGOS_PAGING_H

#include <stdint.h>

void paging_initialize(void);
int paging_set_user_page(uint32_t address, int user, int writable);
uint32_t paging_fault_address(void);

#endif
