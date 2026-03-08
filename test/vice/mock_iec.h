/*
 * Mock IEC protocol layer for host-side testing.
 *
 * Provides stubs for iec_set_error() and other IEC functions
 * that cbm_dos.c depends on, so it can be compiled for host.
 */
#ifndef MOCK_IEC_H
#define MOCK_IEC_H

#include <stdint.h>
#include <stdbool.h>

/* Get last error set by cbm_dos code */
uint8_t mock_iec_get_error_code(void);
const char *mock_iec_get_error_msg(void);

#endif /* MOCK_IEC_H */
