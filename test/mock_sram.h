#ifndef MOCK_SRAM_H
#define MOCK_SRAM_H

#include <stdint.h>

/* Host-side controls for the SRAM mock (see mock_sram.c). The
 * firmware-facing API itself is declared in sram.h. */
void mock_sram_set_capacity(uint32_t bytes);   /* 32768 or 65536 */
uint8_t *mock_sram_mem(void);

#endif /* MOCK_SRAM_H */
