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
#include "compress_proto.h"
#include "led_debug.h"

/*
 * 64korppu — Alternative E: Arduino Nano + 23LC512 SPI SRAM
 *
 * C64 IEC serial bus ↔ PC 3.5" HD floppy via ATmega328P.
 * Single-core: IEC and floppy operations are interleaved.
 *
 * Boot sequence:
 *   1. Initialize SPI + SRAM + shift register
 *   2. Initialize IEC bus (device #8) — early, so device responds ASAP
 *   3. Initialize floppy drive (motor on, recalibrate)
 *   4. Read boot sector, mount FAT12
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
void uart_init(void) {
    uint16_t baud_setting = (F_CPU / 8 / 9600) - 1;
    UBRR0H = baud_setting >> 8;
    UBRR0L = baud_setting & 0xFF;
    UCSR0A |= (1 << U2X0);
    UCSR0B = (1 << TXEN0);
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  /* 8N1 */
}

void uart_putchar(char c) {
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void uart_puts(const char *s) {
    while (*s) uart_putchar(*s++);
}

int main(void) {
    /* Disable interrupts during init */
    cli();

    uart_init();
    uart_puts("\r\n=== 64korppu v1.0 (Nano+SRAM) ===\r\n");

    /* Initialize LED debug (green D9, optional red A5) */
    led_debug_init();

    /* Initialize SPI and SRAM */
    sram_init();
    uart_puts("SRAM OK\r\n");

    /* Initialize shift register */
    shiftreg_init();
    uart_puts("74HC595 OK\r\n");

    /* Initialize MFM codec (Timer1) */
    mfm_init();

    /* Initialize IEC bus EARLY — device responds on bus ASAP */
    uart_puts("IEC device #8\r\n");
    iec_init(IEC_DEFAULT_DEVICE);
    cbm_dos_init();

    /* Initialize fast-load protocols */
    fastload_init();
    fastload_jiffydos_register();
    fastload_burst_register();
    fastload_epyx_register();
    uart_puts("Fastload OK\r\n");

    /* Initialize compression protocol */
    compress_proto_init();
    uart_puts("Compress OK\r\n");

    /* Enable interrupts before floppy init (MFM capture needs ISR) */
    sei();

    /* Initialize floppy drive (~2.8s blocking) */
    floppy_init();
    uart_puts("Floppy init...\r\n");

    floppy_motor_on();
    int rc = floppy_recalibrate();
    if (rc == FLOPPY_OK) {
        uart_puts("Drive OK\r\n");
    } else {
        uart_puts("No drive!\r\n");
        led_debug_blink(DBG_FLOPPY_ERROR);
    }

    /* Mount FAT12 */
    uart_puts("Mount FAT12...\r\n");
    rc = fat12_mount();
    if (rc == FAT12_OK) {
        uart_puts("FAT12 OK\r\n");
    } else {
        uart_puts("No disk\r\n");
        led_debug_blink(DBG_NO_DISK);
    }

    uart_puts("Ready.\r\n");
    led_debug_blink(DBG_BOOT_OK);

    /* Main loop: service IEC bus */
    while (1) {
        iec_service();
    }

    return 0;
}

#endif /* __AVR__ */
