#ifndef TEXUGOS_INTERRUPTS_H
#define TEXUGOS_INTERRUPTS_H

#include <stdint.h>

void interrupts_initialize(void);
void interrupt_dispatch(const uint32_t *frame);

#endif
