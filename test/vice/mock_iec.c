/*
 * Mock IEC protocol for host-side testing.
 * Provides stubs for functions that cbm_dos.c and iec_protocol.h declare.
 */
#include "iec_protocol.h"
#include "mock_iec.h"
#include <string.h>
#include <stdio.h>

static uint8_t last_error_code = 0;
static char last_error_msg[64] = "OK";

void iec_set_error(uint8_t code, const char *msg, uint8_t track, uint8_t sector) {
    (void)track; (void)sector;
    last_error_code = code;
    strncpy(last_error_msg, msg, sizeof(last_error_msg) - 1);
    last_error_msg[sizeof(last_error_msg) - 1] = '\0';
}

uint8_t mock_iec_get_error_code(void) {
    return last_error_code;
}

const char *mock_iec_get_error_msg(void) {
    return last_error_msg;
}

/* Stubs for functions declared in iec_protocol.h */
void iec_init(uint8_t device_num) { (void)device_num; }
void iec_service(void) {}
void iec_release_all(void) {}
bool iec_receive_byte_atn(uint8_t *byte) { (void)byte; return false; }
bool iec_receive_byte(uint8_t *byte, bool *eoi) { (void)byte; (void)eoi; return false; }
bool iec_send_byte(uint8_t byte, bool eoi) { (void)byte; (void)eoi; return false; }
