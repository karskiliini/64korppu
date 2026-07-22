/*
 * Host mock of the 23LC512/23LC256 SPI SRAM driver API (sram.h).
 * Backs the firmware's sequential/random access with a plain array.
 * Capacity is settable to model the 23LC256 (32KB, address bit 15
 * ignored → aliasing) as well as the 23LC512 (64KB).
 */
#include <stdint.h>
#include <string.h>
#include "mock_sram.h"

static uint8_t mem[65536];
static uint32_t capacity_bytes = 65536;
static uint32_t seq_addr;

void mock_sram_set_capacity(uint32_t bytes) { capacity_bytes = bytes; }
uint8_t *mock_sram_mem(void) { return mem; }

static inline uint32_t map(uint32_t addr) {
    /* 23LC256 ignores address bit 15 → 0x8000 aliases 0x0000 */
    return addr & (capacity_bytes - 1);
}

void sram_init(void) { memset(mem, 0, sizeof(mem)); seq_addr = 0; }
uint32_t sram_capacity(void) { return capacity_bytes; }

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) buf[i] = mem[map(addr + i)];
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) mem[map(addr + i)] = buf[i];
}

uint8_t sram_read_byte(uint32_t addr) { return mem[map(addr)]; }
void sram_write_byte(uint32_t addr, uint8_t b) { mem[map(addr)] = b; }

void sram_begin_seq_write(uint32_t addr) { seq_addr = addr; }
void sram_seq_write_byte(uint8_t b) { mem[map(seq_addr++)] = b; }
void sram_end_seq(void) {}

void sram_begin_seq_read(uint32_t addr) { seq_addr = addr; }
uint8_t sram_seq_read_byte(void) { return mem[map(seq_addr++)]; }
