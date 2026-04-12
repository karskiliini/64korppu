#include "mfm_codec.h"
#include "config.h"
#include "sram.h"
#include "shiftreg.h"
#include "uart.h"

#ifdef __AVR__

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>

/*
 * MFM codec for ATmega328P — raw interval architecture.
 *
 * Read:   Timer1 ICP1 (D8) captures flux transitions as raw uint8_t
 *         timer intervals → stored sequentially in external SRAM.
 * Decode: Read intervals back from SRAM, calibrate delay+offset
 *         via brute-force, then decode MFM adaptively.
 * Write:  Poll ICP1 to find sector position, then generate MFM
 *         pulses on /WDATA (D7) using Timer1 OC1A for bit-cell timing.
 */

/* ---- Capture state (used by ISR) ---- */

static volatile uint16_t prev_capture;
static volatile uint32_t capture_count;
static volatile bool capture_done;

/* ---- Calibration results (set after capture, used by decoder) ---- */

static uint8_t cal_delay;      /* Best delay value from brute-force calibration */
static uint8_t cal_offset;     /* Best bit offset (0 or 1) */

/* ---- Write state ---- */

static uint8_t write_prev_bit;

/* ---- Dynamic MFM thresholds (used by write-side polling only) ---- */

static volatile uint16_t mfm_thr_short;
static volatile uint16_t mfm_thr_medium;
static volatile uint16_t mfm_thr_long;

/* ---- CRC ---- */

uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc) {
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ MFM_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/* ---- Init ---- */

void mfm_init(void) {
    /* Timer1: Normal mode, no prescaler (16MHz clock) */
    TCCR1A = 0;
    TCCR1B = (1 << CS10);  /* No prescaler */

    /* ICP1 input on PB0 (D8), falling edge trigger */
    TCCR1B &= ~(1 << ICES1);  /* Falling edge */
    /* ICNC1 disabled — noise canceler adds variable delay with slow signals */

    /* Disable capture interrupt initially */
    TIMSK1 &= ~(1 << ICIE1);

    /* Initialize thresholds for write-side polling (from compile-time defaults) */
    mfm_thr_short  = MFM_THRESHOLD_SHORT;
    mfm_thr_medium = MFM_THRESHOLD_MEDIUM;
    mfm_thr_long   = MFM_THRESHOLD_LONG;

    /* Default calibration values (overwritten after first capture) */
    cal_delay  = MFM_PULLUP_DELAY;
    cal_offset = 1;
}

/* ---- Capture ISR: store raw uint8_t intervals to SRAM ---- */

/*
 * Minimal ISR: compute interval, store low byte to SRAM via SPI.
 * No classification, no packing — just raw timer ticks.
 * Intervals are always < 256 ticks at 16MHz / HD 500kbps.
 */
ISR(TIMER1_CAPT_vect) {
    uint16_t ts = ICR1;
    uint16_t interval = ts - prev_capture;
    prev_capture = ts;

    /* Store low byte — intervals are < 256 for valid MFM pulses */
    SPDR = (uint8_t)interval;
    while (!(SPSR & (1 << SPIF)));
    capture_count++;

    if (capture_count >= SRAM_RAW_CAPTURE_SIZE) {
        TIMSK1 &= ~(1 << ICIE1);
        capture_done = true;
    }
}

/* ---- Brute-force calibration ---- */

/*
 * Try all delay values 50..120 and both offsets 0..1.
 * For each combination, classify first CAL_SAMPLE_COUNT intervals,
 * build MFM bitstream, extract bytes, and score by recognizing
 * standard MFM gap/sync/marker bytes.
 *
 * Reads raw intervals from SRAM via sequential read.
 */
#define CAL_SAMPLE_COUNT  1000
#define CAL_BYTE_COUNT    64     /* Max bytes to extract per trial */

static void mfm_calibrate(void) {
    uint16_t best_score = 0;
    uint8_t best_delay = MFM_PULLUP_DELAY;
    uint8_t best_offset = 1;

    /* Read first CAL_SAMPLE_COUNT intervals into a small
     * working window. We can't keep all 1000 in RAM on AVR,
     * so we re-read from SRAM for each delay trial.
     * This is slow but only done once per track. */

    for (uint8_t delay = 50; delay <= 120; delay++) {
        for (uint8_t offset = 0; offset <= 1; offset++) {
            uint16_t score = 0;

            sram_begin_seq_read(SRAM_RAW_CAPTURE);

            uint32_t raw_bits = 0;
            uint8_t raw_avail = 0;
            uint8_t dbyte = 0;
            uint8_t dbits = 0;

            for (uint16_t i = 0; i < CAL_SAMPLE_COUNT; i++) {
                uint8_t interval = sram_seq_read_byte();

                /* Classify: cells = (interval - delay + 16) / 32 */
                int16_t adj = (int16_t)interval - (int16_t)delay + 16;
                uint8_t cells;
                if (adj < 0) {
                    cells = 2;  /* Clamp minimum */
                } else {
                    cells = (uint8_t)(adj / 32);
                    if (cells < 2) cells = 2;
                    if (cells > 4) continue;  /* Skip invalid */
                }

                /* Add to MFM bitstream: shift by cells, set LSB */
                raw_bits = (raw_bits << cells) | 1;
                raw_avail += cells;
                if (raw_avail > 30) raw_avail = 30;

                /* Extract data bits at given offset */
                while (raw_avail >= 2) {
                    raw_avail -= 2;
                    uint8_t data_bit = (raw_bits >> (raw_avail + offset)) & 1;
                    dbyte = (dbyte << 1) | data_bit;
                    dbits++;
                    if (dbits >= 8) {
                        /* Score recognized bytes */
                        switch (dbyte) {
                            case 0x4E: score += 10; break;  /* Gap fill */
                            case 0x00: score += 5;  break;  /* Preamble */
                            case 0xA1: score += 20; break;  /* Sync mark */
                            case 0xFE: score += 15; break;  /* IDAM */
                            case 0xFB: score += 15; break;  /* DAM */
                        }
                        dbyte = 0;
                        dbits = 0;
                    }
                }
            }

            sram_end_seq();

            if (score > best_score) {
                best_score = score;
                best_delay = delay;
                best_offset = offset;
            }
        }
    }

    cal_delay = best_delay;
    cal_offset = best_offset;

    TRACE("[MFM] cal: best delay=");
    uart_putdec(best_delay);
    TRACE(" off=");
    uart_putdec(best_offset);
    TRACE(" score=");
    uart_putdec(best_score);
    TRACE("\r\n");
}

/* ---- Capture track ---- */

int mfm_capture_track(void) {
    TRACE("[MFM] capture start\r\n");

    capture_count = 0;
    capture_done = false;

    /* Start sequential SRAM write at raw capture buffer */
    sram_begin_seq_write(SRAM_RAW_CAPTURE);

    /* Clear pending capture flag, seed prev_capture */
    TIFR1 |= (1 << ICF1);
    prev_capture = ICR1;

    /* Enable input capture interrupt */
    TIMSK1 |= (1 << ICIE1);
    sei();

    /* Wait for capture to complete or timeout */
    uint16_t timeout = 0;
    while (!capture_done && timeout < 500) {
        _delay_ms(1);
        timeout++;
    }

    TIMSK1 &= ~(1 << ICIE1);
    sram_end_seq();

    TRACE("[MFM] capture: ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" intervals, ");
    uart_putdec(timeout);
    TRACE("ms\r\n");

    if (timeout >= 500) {
        TRACE("[MFM] capture TIMEOUT!\r\n");
        return FLOPPY_ERR_TIMEOUT;
    }

    /* Raw interval histogram: 8 bins covering 0..255 (bin width = 32) */
    {
        uint16_t hist[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        uint16_t scan = (capture_count > 500) ? 500 : (uint16_t)capture_count;

        sram_begin_seq_read(SRAM_RAW_CAPTURE);
        for (uint16_t i = 0; i < scan; i++) {
            uint8_t v = sram_seq_read_byte();
            uint8_t bin = v >> 5;  /* v / 32 */
            if (bin < 8) hist[bin]++;
        }
        sram_end_seq();

        TRACE("[MFM] hist:");
        for (uint8_t b = 0; b < 8; b++) {
            uart_putchar(' ');
            uart_putdec((uint16_t)b * 32);
            uart_putchar('-');
            uart_putdec((uint16_t)(b + 1) * 32 - 1);
            uart_putchar('=');
            uart_putdec(hist[b]);
        }
        TRACE("\r\n");
    }

    /* Brute-force calibration on first 1000 intervals */
    mfm_calibrate();

    /* Dump first 200 decoded bytes using calibrated delay+offset.
     * Covers ~3 sectors — should show 4E gap, 00 preamble, A1 sync, FE IDAM. */
    {
        uint16_t sample = (capture_count > 10000) ? 10000 : (uint16_t)capture_count;
        sram_begin_seq_read(SRAM_RAW_CAPTURE);
        uint32_t raw_bits = 0;
        uint8_t avail = 0, dbyte = 0, dbits = 0;
        uint16_t dcount = 0;

        TRACE("[MFM] decoded (first 200 bytes, delay=");
        uart_putdec(cal_delay);
        TRACE(" off=");
        uart_putdec(cal_offset);
        TRACE("):\r\n");

        for (uint16_t i = 0; i < sample && dcount < 200; i++) {
            uint8_t v = sram_seq_read_byte();
            int16_t adj = (int16_t)v - (int16_t)cal_delay + 16;
            uint8_t cells;
            if (adj < 32) cells = 2;
            else {
                cells = (uint8_t)(adj / 32);
                if (cells > 4) continue;
            }

            raw_bits = (raw_bits << cells) | 1;
            avail += cells;
            if (avail > 30) avail = 30;

            while (avail >= 2 && dcount < 200) {
                avail -= 2;
                dbyte = (dbyte << 1) | ((raw_bits >> (avail + cal_offset)) & 1);
                dbits++;
                if (dbits >= 8) {
                    uart_puthex8(dbyte);
                    uart_putchar(' ');
                    dcount++;
                    if ((dcount % 24) == 0) TRACE("\r\n");
                    dbyte = 0; dbits = 0;
                }
            }
        }
        sram_end_seq();
        TRACE("\r\n");
    }

    return FLOPPY_OK;
}

/* ---- Decode (SRAM → sector data) ---- */

/*
 * Decode state machine.
 * Reads raw uint8_t intervals from SRAM, classifies using calibrated
 * delay, builds MFM bitstream, extracts data bytes at calibrated offset,
 * and uses brute-force IDAM search to find sector headers.
 */
typedef enum {
    DEC_SCAN_SYNC,   /* Looking for IDAM (0xFE) via brute-force alignment */
    DEC_READ_ID,     /* Extracting 4 sector ID bytes (T, S, R, N) */
    DEC_READ_DATA,   /* Extracting 512 data bytes after DAM */
} decode_state_t;

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    TRACE("[MFM] decode sector ");
    uart_putdec(sector);
    TRACE(", ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" intervals, delay=");
    uart_putdec(cal_delay);
    TRACE(" off=");
    uart_putdec(cal_offset);
    TRACE("\r\n");

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    bool found_sector = false;

    decode_state_t state = DEC_SCAN_SYNC;
    uint8_t byte_val = 0;
    uint8_t byte_bits = 0;
    uint8_t field_bytes[4];
    uint8_t field_pos = 0;
    uint16_t data_pos = 0;
    uint8_t offset = cal_offset;
    uint8_t delay = cal_delay;

    sram_begin_seq_read(SRAM_RAW_CAPTURE);

    for (uint32_t pos = 0; pos < capture_count; pos++) {
        uint8_t interval = sram_seq_read_byte();

        /* Classify interval using calibrated delay */
        int16_t adj = (int16_t)interval - (int16_t)delay + 16;
        uint8_t cells;
        if (adj < 0) {
            cells = 2;  /* Clamp minimum */
        } else {
            cells = (uint8_t)(adj / 32);
            if (cells < 2) cells = 2;
            if (cells > 4) {
                /* Invalid interval — reset state */
                raw_bits = 0;
                raw_avail = 0;
                if (state == DEC_SCAN_SYNC) continue;
                state = DEC_SCAN_SYNC;
                byte_bits = 0;
                continue;
            }
        }

        /* Add to MFM bitstream */
        raw_bits = (raw_bits << cells) | 1;
        raw_avail += cells;
        if (raw_avail > 30) raw_avail = 30;

        if (state == DEC_SCAN_SYNC) {
            /*
             * Brute-force marker search: extract byte at the calibrated
             * offset from current raw_bits. Need 16 MFM bits = 8 data bits.
             *
             * When found_sector is false: look for IDAM (0xFE).
             * When found_sector is true:  look for DAM (0xFB).
             */
            if (raw_avail >= 18) {
                /* Extract byte at calibrated offset */
                uint8_t test = 0;
                for (uint8_t b = 0; b < 8; b++) {
                    uint8_t bitpos = offset + (7 - b) * 2;
                    test |= (uint8_t)(((raw_bits >> bitpos) & 1) << (7 - b));
                }
                if (!found_sector && test == MFM_IDAM) {
                    /* Found IDAM marker -- switch to reading sector ID */
                    state = DEC_READ_ID;
                    field_pos = 0;
                    byte_val = 0;
                    byte_bits = 0;
                    raw_avail = 0;  /* Reset: start fresh for ID bytes */
                } else if (found_sector && test == MFM_DAM) {
                    /* Found DAM after matching IDAM -- start reading data */
                    state = DEC_READ_DATA;
                    data_pos = 0;
                    byte_val = 0;
                    byte_bits = 0;
                    raw_avail = 0;
                }
            }
        } else {
            /* States DEC_READ_ID / DEC_READ_DATA:
             * Extract data bits from raw MFM stream at calibrated offset.
             * Every 2 raw MFM bits = 1 data bit (clock+data pair). */
            while (raw_avail >= 2) {
                raw_avail -= 2;
                uint8_t data_bit = (raw_bits >> (raw_avail + offset)) & 0x01;
                byte_val = (byte_val << 1) | data_bit;
                byte_bits++;

                if (byte_bits >= 8) {
                    /* Complete byte extracted */
                    byte_bits = 0;

                    if (state == DEC_READ_ID) {
                        field_bytes[field_pos++] = byte_val;
                        if (field_pos >= 4) {
                            uint8_t t = field_bytes[0];
                            uint8_t s = field_bytes[1];
                            uint8_t r = field_bytes[2];
                            uint8_t n = field_bytes[3];

                            /* Validate: T=0-79, S=0-1, R=1-18, N=2 */
                            if (t < 80 && s <= 1 && r >= 1 && r <= 18 && n == 2) {
                                TRACE("[MFM] IDAM: T=");
                                uart_putdec(t);
                                TRACE(" S=");
                                uart_putdec(s);
                                TRACE(" R=");
                                uart_putdec(r);
                                TRACE("\r\n");
                                if (r == sector) {
                                    TRACE("[MFM] SECTOR MATCH!\r\n");
                                    found_sector = true;
                                    /* Continue scanning for DAM (0xFB) */
                                }
                            }
                            /* Back to scanning (for DAM or next IDAM) */
                            state = DEC_SCAN_SYNC;
                            break;  /* Exit bit extraction, resume scanning */
                        }
                    } else if (state == DEC_READ_DATA) {
                        data_out[data_pos++] = byte_val;
                        if (data_pos >= 512) {
                            TRACE("[MFM] decode OK, 512 bytes\r\n");
                            sram_end_seq();
                            return FLOPPY_OK;
                        }
                    }
                    byte_val = 0;
                }
            }
        }
    }

    sram_end_seq();
    TRACE("[MFM] decode FAIL: sector ");
    uart_putdec(sector);
    TRACE(" not found\r\n");
    return FLOPPY_ERR_NO_SECTOR;
}

/* ---- Find sectors (SRAM → ID list) ---- */

int mfm_find_sectors(mfm_sector_id_t *ids_out, int max_ids) {
    int found = 0;

    /* TODO: implement using same raw-interval decode as mfm_decode_sector */
    (void)ids_out;
    (void)max_ids;

    return found;
}

/* ---- Write helpers ---- */

/*
 * /WDATA pulse: short negative pulse (~300ns).
 * Drive detects falling edge as flux transition.
 */
static inline void wdata_pulse(void) {
    FLOPPY_WDATA_PORT &= ~(1 << FLOPPY_WDATA_PIN);
    __builtin_avr_delay_cycles(5);  /* ~300ns at 16MHz */
    FLOPPY_WDATA_PORT |= (1 << FLOPPY_WDATA_PIN);
}

/*
 * Wait for one MFM bit cell (2us = 32 Timer1 ticks at 16MHz).
 * Uses OC1A compare match for jitter-free timing.
 */
static inline void wait_bit_cell(void) {
    while (!(TIFR1 & (1 << OCF1A)));
    TIFR1 = (1 << OCF1A);
    OCR1A += MFM_TICKS_PER_CELL_HD;
}

/*
 * Write one data byte as MFM, tracking clock insertion.
 * MFM encoding: clock bit inserted when prev_data=0 AND current_data=0.
 */
static void mfm_write_data_byte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t data_bit = (byte >> i) & 1;

        /* Clock bit cell */
        wait_bit_cell();
        if (!write_prev_bit && !data_bit) {
            wdata_pulse();
        }

        /* Data bit cell */
        wait_bit_cell();
        if (data_bit) {
            wdata_pulse();
        }

        write_prev_bit = data_bit;
    }
}

/*
 * Write MFM sync byte 0xA1 with missing clock bit.
 * Standard MFM for 0xA1: 0100_0100_1010_1001
 * Sync variant:           0100_0100_1000_1001 (clock bit 5 missing)
 * Hex: 0x4489
 */
static void mfm_write_sync_a1(void) {
    uint16_t pattern = 0x4489;
    for (int8_t i = 15; i >= 0; i--) {
        wait_bit_cell();
        if (pattern & (1U << i)) {
            wdata_pulse();
        }
    }
    write_prev_bit = 1;  /* Last data bit of 0xA1 is 1 */
}

/*
 * Poll ICP1 for one flux transition, classify interval.
 * Returns pulse code (0=2T, 1=3T, 2=4T, 3=invalid).
 * Used for real-time sector search during write positioning.
 */
static uint8_t mfm_poll_pulse(void) {
    TIFR1 = (1 << ICF1);
    uint16_t timeout = 0;
    while (!(TIFR1 & (1 << ICF1))) {
        if (++timeout > 50000) return 3;
    }
    uint16_t interval = ICR1 - prev_capture;
    prev_capture = ICR1;

    if (interval < mfm_thr_short) return 0;
    if (interval < mfm_thr_medium) return 1;
    if (interval < mfm_thr_long) return 2;
    return 3;
}

/*
 * Scan /RDATA in real-time (polling) for target sector IDAM.
 * Returns FLOPPY_OK when positioned right after the 4-byte sector ID.
 * Caller should then wait through CRC+gap2 before writing.
 */
#define MFM_RAW_SYNC  0x4489

static int mfm_wait_for_sector(uint8_t target_sector) {
    TRACE("[MFM] scan for sector ");
    uart_putdec(target_sector);
    TRACE("...\r\n");

    prev_capture = ICR1;

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;

    /* Scan up to ~2 revolutions (80000 pulses) */
    for (uint32_t n = 0; n < 80000; n++) {
        uint8_t code = mfm_poll_pulse();

        switch (code) {
            case 0: raw_bits = (raw_bits << 2) | 0x01; raw_avail += 2; break;
            case 1: raw_bits = (raw_bits << 3) | 0x01; raw_avail += 3; break;
            case 2: raw_bits = (raw_bits << 4) | 0x01; raw_avail += 4; break;
            default: raw_bits = 0; raw_avail = 0; sync_count = 0; continue;
        }
        if (raw_avail > 30) raw_avail = 30;

        if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
            sync_count++;
            if (sync_count >= 3) {
                /* Read address mark + 4 ID bytes via polling */
                raw_avail = 0;
                uint8_t mark_bits = 0;
                uint8_t mark = 0;
                while (mark_bits < 16) {
                    code = mfm_poll_pulse();
                    switch (code) {
                        case 0: raw_bits = (raw_bits << 2) | 0x01; raw_avail += 2; break;
                        case 1: raw_bits = (raw_bits << 3) | 0x01; raw_avail += 3; break;
                        case 2: raw_bits = (raw_bits << 4) | 0x01; raw_avail += 4; break;
                        default: goto resync;
                    }
                    while (raw_avail >= 2 && mark_bits < 16) {
                        raw_avail -= 2;
                        mark = (mark << 1) | ((raw_bits >> raw_avail) & 1);
                        mark_bits += 2;
                    }
                }

                if (mark == MFM_IDAM) {
                    uint8_t id_bytes[4];
                    for (uint8_t f = 0; f < 4; f++) {
                        uint8_t byte_val = 0;
                        uint8_t bits = 0;
                        while (bits < 16) {
                            code = mfm_poll_pulse();
                            switch (code) {
                                case 0: raw_bits = (raw_bits << 2) | 0x01; raw_avail += 2; break;
                                case 1: raw_bits = (raw_bits << 3) | 0x01; raw_avail += 3; break;
                                case 2: raw_bits = (raw_bits << 4) | 0x01; raw_avail += 4; break;
                                default: goto resync;
                            }
                            while (raw_avail >= 2 && bits < 16) {
                                raw_avail -= 2;
                                byte_val = (byte_val << 1) | ((raw_bits >> raw_avail) & 1);
                                bits += 2;
                            }
                        }
                        id_bytes[f] = byte_val;
                    }

                    if (id_bytes[2] == target_sector) {
                        TRACE("[MFM] found sector ");
                        uart_putdec(target_sector);
                        TRACE(" at T=");
                        uart_putdec(id_bytes[0]);
                        TRACE("\r\n");
                        return FLOPPY_OK;
                    }
                }
            resync:
                sync_count = 0;
            }
        }
    }

    TRACE("[MFM] sector scan timeout\r\n");
    return FLOPPY_ERR_TIMEOUT;
}

/* ---- Write sector ---- */

int mfm_write_sector(const mfm_sector_id_t *id, const uint8_t *data) {
    /*
     * Write data field of an existing sector:
     * 1. Poll /RDATA to find target IDAM in real-time
     * 2. Wait through ID CRC (2 bytes) + gap2 (22 bytes) = 768us
     * 3. Assert /WGATE, disable interrupts
     * 4. Write: 12x0x00 + 3xsync_A1 + DAM + 512xdata + CRC + gap3
     * 5. Release /WGATE, re-enable interrupts
     */
    TRACE("[MFM] write T=");
    uart_putdec(id->track);
    TRACE(" S=");
    uart_putdec(id->side);
    TRACE(" R=");
    uart_putdec(id->sector);
    TRACE("\r\n");

    /* Step 1: Find sector IDAM by polling /RDATA */
    int rc = mfm_wait_for_sector(id->sector);
    if (rc != FLOPPY_OK) return rc;

    /* Step 2: Wait through ID CRC (2 bytes) + gap2 (22 bytes)
     * = 24 data bytes x 32us/byte = 768us */
    _delay_us(768);

    /* Step 3: Assert /WGATE (via shift register) */
    shiftreg_assert_bit(SR_BIT_WGATE);

    cli();

    /* Initialize Timer1 OC1A for precise bit-cell timing */
    write_prev_bit = 0;
    OCR1A = TCNT1 + MFM_TICKS_PER_CELL_HD;
    TIFR1 = (1 << OCF1A);

    /* Step 4: Write data field */
    /* Preamble: 12 x 0x00 */
    for (uint8_t i = 0; i < 12; i++) {
        mfm_write_data_byte(0x00);
    }

    /* Sync: 3 x 0xA1 (with missing clock) */
    mfm_write_sync_a1();
    mfm_write_sync_a1();
    mfm_write_sync_a1();

    /* DAM */
    mfm_write_data_byte(MFM_DAM);

    /* CRC starts from sync bytes + DAM */
    uint16_t crc = MFM_CRC_INIT;
    uint8_t sync_byte = MFM_SYNC_BYTE;
    crc = mfm_crc16(&sync_byte, 1, crc);
    crc = mfm_crc16(&sync_byte, 1, crc);
    crc = mfm_crc16(&sync_byte, 1, crc);
    uint8_t dam_byte = MFM_DAM;
    crc = mfm_crc16(&dam_byte, 1, crc);

    /* 512 data bytes */
    for (uint16_t i = 0; i < 512; i++) {
        mfm_write_data_byte(data[i]);
        crc = mfm_crc16(&data[i], 1, crc);
    }

    /* CRC (2 bytes, MSB first) */
    uint8_t crc_hi = (crc >> 8) & 0xFF;
    uint8_t crc_lo = crc & 0xFF;
    mfm_write_data_byte(crc_hi);
    mfm_write_data_byte(crc_lo);

    /* Gap3: 54 x 0x4E (HD standard) */
    for (uint8_t i = 0; i < 54; i++) {
        mfm_write_data_byte(0x4E);
    }

    sei();

    /* Step 5: Release /WGATE */
    shiftreg_release_bit(SR_BIT_WGATE);

    TRACE("[MFM] write OK\r\n");
    return FLOPPY_OK;
}

#endif /* __AVR__ */
