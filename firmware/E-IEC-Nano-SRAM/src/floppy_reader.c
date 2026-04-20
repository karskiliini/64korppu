#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

#include "config.h"
#include "sram.h"
#include "shiftreg.h"
#include "floppy_ctrl.h"
#include "mfm_codec.h"

/*
 * Continuous floppy read loop for oscilloscope debugging.
 *
 * Reads the same track/side/sector in an infinite loop so that
 * the /RDATA signal repeats predictably for scope triggering.
 *
 * Build this instead of main.c for diagnostics.
 */

#define READ_TRACK   0
#define READ_SIDE    0
#define READ_SECTOR  1

static uint8_t sector_buf[FLOPPY_SECTOR_SIZE];

/* UART */
static void uart_init(void) {
    uint16_t baud_setting = (F_CPU / 8 / 9600) - 1;
    UBRR0H = baud_setting >> 8;
    UBRR0L = baud_setting & 0xFF;
    UCSR0A |= (1 << U2X0);
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

static void uart_putchar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putchar(*s++);
}

static void uart_puthex(uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_putchar(hex[val >> 4]);
    uart_putchar(hex[val & 0x0F]);
}

static void uart_putdec(uint32_t val) {
    char buf[10];
    int i = 0;
    if (val == 0) { uart_putchar('0'); return; }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) uart_putchar(buf[--i]);
}

int main(void) {
    cli();
    uart_init();

    uart_puts("\r\n=== 64korppu floppy reader ===\r\n");
    uart_puts("Continuous read for scope debugging\r\n");
    uart_puts("T0 S0 Sec1\r\n\r\n");

    sram_init();
    shiftreg_init();
    mfm_init();
    floppy_init();

    floppy_motor_on();
    int rc = floppy_recalibrate();
    if (rc != FLOPPY_OK) {
        uart_puts("ERR: no track 0\r\n");
        while (1);
    }
    uart_puts("Drive OK\r\n");

    /* Seek once, select side once */
    floppy_seek(READ_TRACK);
    floppy_select_side(READ_SIDE);

    sei();

    uint32_t count = 0;

    /*
     * Infinite read loop with quiet gap for scope triggering.
     *
     * Cycle:
     *   1. Deselect drive → RDATA goes quiet (HIGH)
     *   2. Wait 100ms → clear quiet period for scope
     *   3. Reselect drive → RDATA becomes active with MFM data
     *   4. Read sector
     *   5. Repeat
     *
     * Trigger scope on CH1 falling edge — the first RDATA pulse
     * after the quiet gap is always the same track position.
     */
    while (1) {
        count++;

        /* Deselect drive — RDATA goes quiet */
        shiftreg_release_bit(SR_BIT_DRVSEL);
        _delay_ms(100);

        /* Reselect drive — RDATA resumes */
        shiftreg_assert_bit(SR_BIT_DRVSEL);
        _delay_ms(1);

        /* Read sector */
        rc = floppy_read_sector(READ_TRACK, READ_SIDE, READ_SECTOR, sector_buf);

        uart_putdec(count);
        if (rc == FLOPPY_OK) {
            uart_puts(" OK\r\n");
        } else {
            uart_puts(" ERR ");
            uart_putdec((uint32_t)(-(int16_t)rc));
            uart_puts("\r\n");
        }
    }

    return 0;
}

#endif /* __AVR__ */
