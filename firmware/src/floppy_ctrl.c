#include "floppy_ctrl.h"
#include "mfm_codec.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

/* Global state */
static floppy_state_t state = {0};

/* Track buffer for raw MFM data */
uint8_t track_buffer[MFM_RAW_TRACK_SIZE];

/* Decoded sector buffer */
uint8_t sector_buffer[FLOPPY_SECTOR_SIZE];

/* Output pins (directly controlled) */
static const uint8_t output_pins[] = {
    FLOPPY_PIN_DENSITY, FLOPPY_PIN_MOTEA, FLOPPY_PIN_DRVSEL,
    FLOPPY_PIN_MOTOR, FLOPPY_PIN_DIR, FLOPPY_PIN_STEP,
    FLOPPY_PIN_WDATA, FLOPPY_PIN_WGATE, FLOPPY_PIN_SIDE1
};

/* Input pins */
static const uint8_t input_pins[] = {
    FLOPPY_PIN_TRK00, FLOPPY_PIN_WPT, FLOPPY_PIN_RDATA, FLOPPY_PIN_DSKCHG
};

void floppy_init(void) {
    /* Configure output pins: active low, start deasserted (HIGH) */
    for (size_t i = 0; i < sizeof(output_pins); i++) {
        gpio_init(output_pins[i]);
        gpio_set_dir(output_pins[i], GPIO_OUT);
        gpio_put(output_pins[i], 1);  /* Deasserted (active low) */
    }

    /* Configure input pins with pull-ups */
    for (size_t i = 0; i < sizeof(input_pins); i++) {
        gpio_init(input_pins[i]);
        gpio_set_dir(input_pins[i], GPIO_IN);
        gpio_pull_up(input_pins[i]);
    }

    /* Select HD density (LOW = HD for 1.44MB) */
    gpio_put(FLOPPY_PIN_DENSITY, 0);

    /* Select drive B (active low) - common for single drive setups */
    gpio_put(FLOPPY_PIN_DRVSEL, 0);

    state.current_track = 0;
    state.current_side = 0;
    state.motor_on = false;
    state.disk_present = true;  /* Assume present until we know otherwise */
    state.write_protected = false;
    state.initialized = false;
}

void floppy_motor_on(void) {
    if (!state.motor_on) {
        gpio_put(FLOPPY_PIN_MOTEA, 0);   /* Assert motor enable (active low) */
        gpio_put(FLOPPY_PIN_MOTOR, 0);    /* Assert motor on (active low) */
        sleep_ms(FLOPPY_MOTOR_SPIN_MS);   /* Wait for spin-up */
        state.motor_on = true;
    }
}

void floppy_motor_off(void) {
    gpio_put(FLOPPY_PIN_MOTEA, 1);
    gpio_put(FLOPPY_PIN_MOTOR, 1);
    state.motor_on = false;
}

/*
 * Perform one step pulse in the given direction.
 */
static void floppy_step_once(bool direction_inward) {
    /* Set direction: LOW = toward center (higher tracks), HIGH = toward edge */
    gpio_put(FLOPPY_PIN_DIR, direction_inward ? 0 : 1);
    sleep_us(10);  /* Direction setup time */

    /* Step pulse: active low, minimum 3us */
    gpio_put(FLOPPY_PIN_STEP, 0);
    sleep_us(FLOPPY_STEP_PULSE_US);
    gpio_put(FLOPPY_PIN_STEP, 1);

    sleep_ms(FLOPPY_STEP_RATE_MS);  /* Inter-step delay */
}

int floppy_recalibrate(void) {
    /* Step outward until TRK00 is asserted, max 85 steps */
    for (int i = 0; i < 85; i++) {
        if (!gpio_get(FLOPPY_PIN_TRK00)) {
            /* TRK00 is active low - we're at track 0 */
            state.current_track = 0;
            state.initialized = true;
            sleep_ms(FLOPPY_STEP_SETTLE_MS);
            return FLOPPY_OK;
        }
        floppy_step_once(false);  /* Step outward */
    }

    return FLOPPY_ERR_NO_TRK0;
}

int floppy_seek(uint8_t track) {
    if (track >= FLOPPY_TRACKS) {
        return FLOPPY_ERR_SEEK;
    }

    if (!state.initialized) {
        int rc = floppy_recalibrate();
        if (rc != FLOPPY_OK) return rc;
    }

    while (state.current_track != track) {
        if (state.current_track < track) {
            floppy_step_once(true);   /* Step inward */
            state.current_track++;
        } else {
            floppy_step_once(false);  /* Step outward */
            state.current_track--;
        }
    }

    sleep_ms(FLOPPY_STEP_SETTLE_MS);  /* Head settle */
    return FLOPPY_OK;
}

void floppy_select_side(uint8_t side) {
    /* /SIDE1: LOW selects side 1, HIGH selects side 0 */
    gpio_put(FLOPPY_PIN_SIDE1, side ? 0 : 1);
    state.current_side = side;
    sleep_us(100);  /* Side select settle time */
}

int floppy_read_raw_track(void) {
    /*
     * Read raw MFM flux transitions from /RDATA using PIO.
     *
     * The PIO state machine measures time between flux transitions
     * and stores them as pulse widths. DMA transfers the data to
     * track_buffer automatically.
     *
     * At 500kbps data rate, one bit cell = 2us.
     * Valid MFM pulse widths:
     *   4us (short) = bit pattern 10
     *   6us (medium) = bit pattern 100
     *   8us (long) = bit pattern 1000
     *
     * TODO: PIO program is in mfm_read.pio
     */

    /* For now, placeholder - actual PIO read implementation needed */
    /* This will be filled in when PIO programs are finalized */

    return FLOPPY_OK;
}

int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector,
                        uint8_t *buf) {
    if (sector < 1 || sector > FLOPPY_SECTORS) {
        return FLOPPY_ERR_NO_SECTOR;
    }

    /* Check write protect status while we're at it */
    state.write_protected = !gpio_get(FLOPPY_PIN_WPT);

    /* Seek to track */
    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) return rc;

    /* Select side */
    floppy_select_side(side);

    /* Read raw track data */
    rc = floppy_read_raw_track();
    if (rc != FLOPPY_OK) return rc;

    /* Decode MFM and extract sector */
    rc = mfm_decode_sector(track_buffer, MFM_RAW_TRACK_SIZE, sector, buf);
    if (rc != 0) {
        return FLOPPY_ERR_READ;
    }

    return FLOPPY_OK;
}

int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector,
                         const uint8_t *buf) {
    if (sector < 1 || sector > FLOPPY_SECTORS) {
        return FLOPPY_ERR_NO_SECTOR;
    }

    /* Check write protection */
    if (!gpio_get(FLOPPY_PIN_WPT)) {
        return FLOPPY_ERR_WP;
    }

    /* Seek to track */
    int rc = floppy_seek(track);
    if (rc != FLOPPY_OK) return rc;

    /* Select side */
    floppy_select_side(side);

    /*
     * Writing a sector requires:
     * 1. Read the existing track to find the sector position
     * 2. Re-encode the sector data as MFM
     * 3. Write just the sector data field at the correct position
     *
     * TODO: Implement PIO-based sector write
     */

    /* Read existing track first */
    rc = floppy_read_raw_track();
    if (rc != FLOPPY_OK) return rc;

    /* Encode the new sector data */
    mfm_sector_id_t id = {
        .track = track,
        .side = side,
        .sector = sector,
        .size_code = 2,  /* 512 bytes */
    };

    /* TODO: Find sector position in raw track, splice in new data */
    /* This requires precise PIO timing to write at the correct position */

    (void)id;
    (void)buf;

    return FLOPPY_OK;
}

floppy_state_t floppy_get_state(void) {
    /* Update live status */
    state.write_protected = !gpio_get(FLOPPY_PIN_WPT);
    /* DSKCHG is latched - cleared by stepping */
    return state;
}
