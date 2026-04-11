#include "led_debug.h"
#include "config.h"

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

/* Green LED: PB1 (D9) — always present */
#define LED_GREEN_DDR   DDRB
#define LED_GREEN_PORT  PORTB
#define LED_GREEN_PIN   PB1

/* Red LED: PC5 (A5) — optional, no-op if not soldered */
#define LED_RED_DDR     DDRC
#define LED_RED_PORT    PORTC
#define LED_RED_PIN     PC5

#define BLINK_ON_MS     80
#define BLINK_OFF_MS    80
#define GROUP_PAUSE_MS  300

void led_green_on(void)  { LED_GREEN_PORT |=  (1 << LED_GREEN_PIN); }
void led_green_off(void) { LED_GREEN_PORT &= ~(1 << LED_GREEN_PIN); }
void led_red_on(void)    { LED_RED_PORT   |=  (1 << LED_RED_PIN); }
void led_red_off(void)   { LED_RED_PORT   &= ~(1 << LED_RED_PIN); }

static void blink_green(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        led_green_on();
        _delay_ms(BLINK_ON_MS);
        led_green_off();
        _delay_ms(BLINK_OFF_MS);
    }
}

static void blink_red(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        led_red_on();
        _delay_ms(BLINK_ON_MS);
        led_red_off();
        _delay_ms(BLINK_OFF_MS);
    }
}

static void blink_both(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        led_green_on(); led_red_on();
        _delay_ms(BLINK_ON_MS);
        led_green_off(); led_red_off();
        _delay_ms(BLINK_OFF_MS);
    }
}

void led_debug_init(void) {
    LED_GREEN_DDR |= (1 << LED_GREEN_PIN);
    LED_RED_DDR   |= (1 << LED_RED_PIN);
    led_green_off();
    led_red_off();
}

void led_debug_blink(led_debug_code_t code) {
    switch (code) {
        case DBG_BOOT_OK:       blink_green(2); break;
        case DBG_ATN_RECEIVED:  blink_green(1); break;
        case DBG_LISTEN_OK:     blink_green(3); break;
        case DBG_TALK_OK:       blink_both(1);  break;
        case DBG_IEC_TIMEOUT:   blink_red(2);   break;
        case DBG_FLOPPY_ERROR:  blink_red(3);   break;
        case DBG_NO_DISK:       blink_red(1);   break;
    }
    _delay_ms(GROUP_PAUSE_MS);
}

#endif /* __AVR__ */
