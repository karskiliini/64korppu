#include "floppy_ctrl.h"
#include "config.h"
#include "shiftreg.h"
#include "mfm_codec.h"
#include "uart.h"

#ifdef __AVR__

#include <avr/io.h>
#include <util/delay.h>

/*
 * Floppy drive control for Arduino Nano.
 *
 * Output signals (/DENSITY, /MOTEA, /DRVSEL, /MOTOR, /DIR, /STEP,
 * /WGATE, /SIDE1) go through 74HC595 shift register.
 *
 * Input signals (/TRK00, /WPT, /DSKCHG) are direct GPIO on A0-A2.
 * /RDATA on D8 (Timer1 ICP1) for MFM capture.
 * /WDATA on D7 (direct GPIO) for MFM write.
 */

static floppy_state_t state = {0};

void floppy_init(void) {
    TRACE("[FLP] init GPIO...\r\n");

    /* Input pins with pull-ups: /TRK00 (A0), /WPT (A1), /DSKCHG (A2) */
    FLOPPY_IN_DDR &= ~((1 << FLOPPY_TRK00_PIN) |
                        (1 << FLOPPY_WPT_PIN) |
                        (1 << FLOPPY_DSKCHG_PIN));
    FLOPPY_IN_PORT |= (1 << FLOPPY_TRK00_PIN) |
                      (1 << FLOPPY_WPT_PIN) |
                      (1 << FLOPPY_DSKCHG_PIN);

    /* /RDATA (D8 = PB0) as input */
    DDRB &= ~(1 << FLOPPY_RDATA_PIN);

    /* /WDATA (D7 = PD7) as output, HIGH (deasserted) */
    FLOPPY_WDATA_DDR |= (1 << FLOPPY_WDATA_PIN);
    FLOPPY_WDATA_PORT |= (1 << FLOPPY_WDATA_PIN);

    /* LED (D9 = PB1) as output */
    DDRB |= (1 << FLOPPY_LED_PIN);

    /* Set shift register: HD density, drive selected, everything else deasserted */
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);  /* Assert /DENSITY (LOW = HD) */
    sr &= ~(1 << SR_BIT_DRVSEL);   /* Assert /DRVSEL (select drive) */
    shiftreg_write(sr);

    /* Read input pin states for debug */
    uint8_t pins = FLOPPY_IN_PINR;
    TRACE("[FLP] inputs: TRK00=");
    uart_putchar((pins & (1 << FLOPPY_TRK00_PIN)) ? 'H' : 'L');
    TRACE(" WPT=");
    uart_putchar((pins & (1 << FLOPPY_WPT_PIN)) ? 'H' : 'L');
    TRACE(" DSKCHG=");
    uart_putchar((pins & (1 << FLOPPY_DSKCHG_PIN)) ? 'H' : 'L');
    TRACE("\r\n");

    state.current_track = 0;
    state.current_side = 0;
    state.motor_on = false;
    state.disk_present = false;
    state.write_protected = false;
    state.initialized = false;

    TRACE("[FLP] init done, SR=0x");
    uart_puthex8(sr);
    TRACE("\r\n");
}

void floppy_motor_on(void) {
    if (!state.motor_on) {
        TRACE("[FLP] motor ON, spin-up...\r\n");
        shiftreg_assert_bit(SR_BIT_MOTEA);
        shiftreg_assert_bit(SR_BIT_MOTOR);
        _delay_ms(FLOPPY_MOTOR_SPIN_MS);
        state.motor_on = true;
        PORTB |= (1 << FLOPPY_LED_PIN);  /* LED on */
        TRACE("[FLP] motor ON done\r\n");
    }
}

void floppy_motor_off(void) {
    TRACE("[FLP] motor OFF\r\n");
    shiftreg_release_bit(SR_BIT_MOTEA);
    shiftreg_release_bit(SR_BIT_MOTOR);
    state.motor_on = false;
    PORTB &= ~(1 << FLOPPY_LED_PIN);  /* LED off */
}

static void step_once(bool inward) {
    /* Set direction: assert /DIR = inward (toward center) */
    if (inward) {
        shiftreg_assert_bit(SR_BIT_DIR);
    } else {
        shiftreg_release_bit(SR_BIT_DIR);
    }
    _delay_us(10);

    /* Step pulse */
    shiftreg_assert_bit(SR_BIT_STEP);
    _delay_us(FLOPPY_STEP_PULSE_US);
    shiftreg_release_bit(SR_BIT_STEP);
    _delay_ms(FLOPPY_STEP_RATE_MS);
}

int floppy_recalibrate(void) {
    TRACE("[FLP] recalibrate: seeking track 0...\r\n");
    for (int i = 0; i < 85; i++) {
        if (!(FLOPPY_IN_PINR & (1 << FLOPPY_TRK00_PIN))) {
            /* /TRK00 active low - we're at track 0 */
            state.current_track = 0;
            state.initialized = true;
            _delay_ms(FLOPPY_STEP_SETTLE_MS);
            TRACE("[FLP] TRK00 found after ");
            uart_putdec(i);
            TRACE(" steps\r\n");
            return FLOPPY_OK;
        }
        step_once(false);  /* Step outward */
    }
    TRACE("[FLP] ERR: TRK00 not found after 85 steps!\r\n");
    return FLOPPY_ERR_NO_TRK0;
}

bool floppy_check_disk(void) {
    /*
     * Read /DSKCHG (A2) after head has stepped (recalibrate).
     * /DSKCHG is active-low:
     *   LOW  = disk changed / no disk present
     *   HIGH = disk present (cleared by step pulse)
     */
    bool present = !!(FLOPPY_IN_PINR & (1 << FLOPPY_DSKCHG_PIN));
    state.disk_present = present;
    state.write_protected = !(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN));

    TRACE("[FLP] disk check: DSKCHG=");
    uart_putchar(present ? 'H' : 'L');
    TRACE(" WPT=");
    uart_putchar(state.write_protected ? 'Y' : 'N');
    if (present) {
        TRACE(" -> disk PRESENT\r\n");
    } else {
        TRACE(" -> disk NOT PRESENT\r\n");
    }

    return present;
}

int floppy_seek(uint8_t track) {
    if (track >= FLOPPY_TRACKS) {
        TRACE("[FLP] seek ERR: track ");
        uart_putdec(track);
        TRACE(" >= ");
        uart_putdec(FLOPPY_TRACKS);
        TRACE("\r\n");
        return FLOPPY_ERR_SEEK;
    }

    if (!state.initialized) {
        TRACE("[FLP] seek: not initialized, recalibrating\r\n");
        int rc = floppy_recalibrate();
        if (rc != FLOPPY_OK) return rc;
    }

    if (state.current_track != track) {
        TRACE("[FLP] seek T");
        uart_putdec(state.current_track);
        TRACE("->T");
        uart_putdec(track);
        TRACE("\r\n");
    }

    while (state.current_track != track) {
        if (state.current_track < track) {
            step_once(true);
            state.current_track++;
        } else {
            step_once(false);
            state.current_track--;
        }
    }

    _delay_ms(FLOPPY_STEP_SETTLE_MS);
    return FLOPPY_OK;
}

void floppy_select_side(uint8_t side) {
    TRACE("[FLP] side=");
    uart_putdec(side);
    TRACE("\r\n");
    if (side) {
        shiftreg_assert_bit(SR_BIT_SIDE1);   /* LOW = side 1 */
    } else {
        shiftreg_release_bit(SR_BIT_SIDE1);  /* HIGH = side 0 */
    }
    state.current_side = side;
    _delay_us(100);
}

int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector,
                        uint8_t *buf) {
    TRACE("[FLP] read T=");
    uart_putdec(track);
    TRACE(" S=");
    uart_putdec(side);
    TRACE(" R=");
    uart_putdec(sector);
    TRACE("\r\n");

    if (sector < 1 || sector > FLOPPY_SECTORS_HD) {
        TRACE("[FLP] read ERR: bad sector\r\n");
        return FLOPPY_ERR_NO_SECTOR;
    }

    floppy_motor_on();

    /* Check write protect */
    state.write_protected = !(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN));
    TRACE("[FLP] WP=");
    uart_putchar(state.write_protected ? 'Y' : 'N');
    TRACE("\r\n");

    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) {
        TRACE("[FLP] read: seek failed\r\n");
        return rc;
    }

    floppy_select_side(side);

    /* Capture raw MFM track to SRAM */
    TRACE("[FLP] MFM capture...\r\n");
    rc = mfm_capture_track();
    if (rc != 0) {
        TRACE("[FLP] MFM capture FAIL rc=");
        uart_putdec((uint16_t)(-rc));
        TRACE("\r\n");
        return FLOPPY_ERR_READ;
    }
    TRACE("[FLP] MFM capture OK\r\n");

    /* Decode sector from SRAM track buffer */
    TRACE("[FLP] MFM decode R=");
    uart_putdec(sector);
    TRACE("...\r\n");
    rc = mfm_decode_sector(sector, buf);
    if (rc != 0) {
        TRACE("[FLP] MFM decode FAIL\r\n");
        return FLOPPY_ERR_READ;
    }
    TRACE("[FLP] read OK, first bytes: ");
    uart_puthex8(buf[0]);
    uart_putchar(' ');
    uart_puthex8(buf[1]);
    uart_putchar(' ');
    uart_puthex8(buf[2]);
    uart_putchar(' ');
    uart_puthex8(buf[3]);
    TRACE("\r\n");

    return FLOPPY_OK;
}

int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector,
                         const uint8_t *buf) {
    TRACE("[FLP] write T=");
    uart_putdec(track);
    TRACE(" S=");
    uart_putdec(side);
    TRACE(" R=");
    uart_putdec(sector);
    TRACE("\r\n");

    if (sector < 1 || sector > FLOPPY_SECTORS_HD) {
        TRACE("[FLP] write ERR: bad sector\r\n");
        return FLOPPY_ERR_NO_SECTOR;
    }

    floppy_motor_on();

    if (!(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN))) {
        TRACE("[FLP] write ERR: write protected!\r\n");
        return FLOPPY_ERR_WP;
    }

    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) {
        TRACE("[FLP] write: seek failed\r\n");
        return rc;
    }

    floppy_select_side(side);

    /* Encode and write sector */
    mfm_sector_id_t id = {
        .track = track,
        .side = side,
        .sector = sector,
        .size_code = 2,
    };

    TRACE("[FLP] MFM write...\r\n");
    rc = mfm_write_sector(&id, buf);
    if (rc != 0) {
        TRACE("[FLP] MFM write FAIL\r\n");
        return FLOPPY_ERR_WRITE;
    }
    TRACE("[FLP] write OK\r\n");

    return FLOPPY_OK;
}

floppy_state_t floppy_get_state(void) {
    state.write_protected = !(FLOPPY_IN_PINR & (1 << FLOPPY_WPT_PIN));
    return state;
}

#endif /* __AVR__ */
