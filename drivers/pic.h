#ifndef TEXUGOS_PIC_H
#define TEXUGOS_PIC_H

#include <stdint.h>

void pic_initialize(void);
void pic_send_eoi(uint32_t irq);

#endif
