#ifndef LED_DEBUG_H
#define LED_DEBUG_H

/*
 * LED debug patterns for 64korppu diagnostics.
 *
 * Green LED: D9 / PB1 (always present, floppy activity LED)
 * Red LED:   A5 / PC5 (optional, solder to A5 + 330Ω + GND)
 *
 * Blink codes:
 *   BOOT_OK       — 2× green       = firmware started, IEC ready
 *   ATN_RECEIVED  — 1× green       = ATN signal detected from C64
 *   LISTEN_OK     — 3× green       = LISTEN #8 acknowledged
 *   TALK_OK       — 1× green+red   = TALK #8 acknowledged
 *   IEC_TIMEOUT   — 2× red         = IEC byte receive timeout
 *   FLOPPY_ERROR  — 3× red         = floppy init or read error
 *   NO_DISK       — 1× red         = no disk / FAT12 mount failed
 *
 * Each blink = 80ms on + 80ms off. Groups separated by 300ms pause.
 */

#include <stdint.h>

typedef enum {
    DBG_BOOT_OK,
    DBG_ATN_RECEIVED,
    DBG_LISTEN_OK,
    DBG_TALK_OK,
    DBG_IEC_TIMEOUT,
    DBG_FLOPPY_ERROR,
    DBG_NO_DISK,
} led_debug_code_t;

void led_debug_init(void);
void led_debug_blink(led_debug_code_t code);

/* Non-blocking: toggle LEDs for real-time IEC activity indication */
void led_green_on(void);
void led_green_off(void);
void led_red_on(void);
void led_red_off(void);

#endif /* LED_DEBUG_H */
