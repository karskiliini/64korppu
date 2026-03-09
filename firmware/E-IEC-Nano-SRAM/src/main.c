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
#include "fat12.h"
#include "iec_protocol.h"
#include "cbm_dos.h"
#include "fastload.h"
#include "fastload_jiffydos.h"
#include "fastload_burst.h"
#include "fastload_epyx.h"

/*
 * 64korppu — Alternative E: Arduino Nano + 23LC256 SPI SRAM
 *
 * C64 IEC serial bus ↔ PC 3.5" HD floppy via ATmega328P.
 * Single-core: IEC and floppy operations are interleaved.
 *
 * Boot sequence:
 *   1. Initialize SPI + SRAM + shift register
 *   2. Initialize floppy drive (motor on, recalibrate)
 *   3. Read boot sector, mount FAT12
 *   4. Initialize IEC bus (device #8)
 *   5. Enter main loop: service IEC bus
 */

/* (sector buffer is in fat12.c as static) */

/*
 * Disk I/O interface for FAT12 layer.
 * Converts LBA to CHS and reads/writes via floppy controller.
 */
int disk_read_sector(uint16_t lba, uint8_t *buf) {
    uint8_t track, side, sector;
    floppy_lba_to_chs(lba, &track, &side, &sector);
    return floppy_read_sector(track, side, sector, buf);
}

int disk_write_sector(uint16_t lba, const uint8_t *buf) {
    uint8_t track, side, sector;
    floppy_lba_to_chs(lba, &track, &side, &sector);
    return floppy_write_sector(track, side, sector, (uint8_t *)buf);
}

/*
 * UART initialization for debug output (9600 baud at 16MHz).
 */
static void uart_init(void) {
    uint16_t baud_setting = (F_CPU / 8 / 9600) - 1;
    UBRR0H = baud_setting >> 8;
    UBRR0L = baud_setting & 0xFF;
    UCSR0A |= (1 << U2X0);
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  /* 8N1 */
}

static void uart_putchar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

static void uart_puts(const char *s) {
    while (*s) uart_putchar(*s++);
}

int main(void) {
    /* Disable interrupts during init */
    cli();

    uart_init();
    uart_puts("\r\n=== 64korppu v1.0 (Nano+SRAM) ===\r\n");

    /* Initialize SPI and SRAM */
    sram_init();
    uart_puts("SRAM OK\r\n");

    /* Initialize shift register */
    shiftreg_init();
    uart_puts("74HC595 OK\r\n");

    /* Initialize MFM codec (Timer1) */
    mfm_init();

    /* Initialize floppy drive */
    floppy_init();
    uart_puts("Floppy init...\r\n");

    floppy_motor_on();
    int rc = floppy_recalibrate();
    if (rc == FLOPPY_OK) {
        uart_puts("Drive OK\r\n");
    } else {
        uart_puts("No drive!\r\n");
    }

    /* Mount FAT12 */
    uart_puts("Mount FAT12...\r\n");
    rc = fat12_mount();
    if (rc == FAT12_OK) {
        uart_puts("FAT12 OK\r\n");
    } else {
        uart_puts("No disk\r\n");
    }

    /* Initialize IEC bus */
    uart_puts("IEC device #8\r\n");
    iec_init(IEC_DEFAULT_DEVICE);
    cbm_dos_init();

    /* Initialize fast-load protocols */
    fastload_init();
    fastload_jiffydos_register();
    fastload_burst_register();
    fastload_epyx_register();
    uart_puts("Fastload OK\r\n");

    /* Enable interrupts */
    sei();

    uart_puts("Ready.\r\n");

    /* Main loop: service IEC bus */
    while (1) {
        iec_service();
    }

    return 0;
}

#endif /* __AVR__ */
