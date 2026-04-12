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
 * MFM codec for ATmega328P.
 *
 * Read:   Timer1 ICP1 (D8) captures flux transitions → SRAM.
 * Decode: Reconstruct MFM bitstream from SRAM, extract sectors.
 * Write:  Poll ICP1 to find sector position, then generate MFM
 *         pulses on /WDATA (D7) using Timer1 OC1A for bit-cell timing.
 */

/* ---- Capture state (used by ISR) ---- */

static volatile uint16_t prev_capture;
static volatile uint32_t capture_count;
static volatile uint8_t pulse_pack;
static volatile uint8_t pulse_in_pack;
static volatile bool capture_done;

/* ---- Dynamic MFM thresholds (ISR reads these) ---- */

static volatile uint16_t mfm_thr_short;
static volatile uint16_t mfm_thr_medium;
static volatile uint16_t mfm_thr_long;
static volatile uint16_t mfm_adaptive_bump;  /* replaces hardcoded +24 */

/* ---- Write state ---- */

static uint8_t write_prev_bit;

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

    /* Initialize dynamic thresholds from compile-time defaults */
    mfm_thr_short  = MFM_THRESHOLD_SHORT;
    mfm_thr_medium = MFM_THRESHOLD_MEDIUM;
    mfm_thr_long   = MFM_THRESHOLD_LONG;
    mfm_adaptive_bump = 24;
}

/* ---- Capture (ISR-driven, track → SRAM) ---- */

/* Raw interval capture for first N pulses (debug) */
#define RAW_INTERVAL_COUNT 200
static volatile uint16_t raw_intervals[RAW_INTERVAL_COUNT];
static volatile uint16_t raw_interval_idx;
static volatile uint8_t prev_code;

ISR(TIMER1_CAPT_vect) {
    uint16_t interval = ICR1 - prev_capture;
    prev_capture = ICR1;

    /* Store raw interval for debug */
    if (raw_interval_idx < RAW_INTERVAL_COUNT) {
        raw_intervals[raw_interval_idx++] = interval;
    }

    /*
     * Adaptive thresholds: after a short interval (2T/3T), the signal
     * recovers less → next interval appears longer. Raise thresholds
     * after short recovery to correctly classify delayed 2T as 2T.
     *
     * After 4T+ (long recovery): standard thresholds
     * After 2T/3T (short recovery): raised thresholds (calibrated bump)
     */
    uint16_t thr_short = mfm_thr_short;
    uint16_t thr_medium = mfm_thr_medium;
    uint16_t thr_long = mfm_thr_long;

    if (prev_code <= 1) {  /* Previous was 2T or 3T → short recovery */
        thr_short += mfm_adaptive_bump;
    }

    uint8_t code;
    if (interval < thr_short) {
        code = 0;  /* 2T */
    } else if (interval < thr_medium) {
        code = 1;  /* 3T */
    } else if (interval < thr_long) {
        code = 2;  /* 4T */
    } else {
        code = 3;  /* Invalid/gap */
    }
    prev_code = code;

    pulse_pack = (pulse_pack << 2) | code;
    pulse_in_pack++;

    if (pulse_in_pack >= 4) {
        SPDR = pulse_pack;
        while (!(SPSR & (1 << SPIF)));
        capture_count++;
        pulse_in_pack = 0;
    }

    if (capture_count >= (SRAM_MFM_TRACK_SIZE - 16)) {
        TIMSK1 &= ~(1 << ICIE1);
        capture_done = true;
    }
}

/*
 * Calibrate MFM thresholds from raw_intervals[].
 *
 * Algorithm: histogram with 16 bins covering ticks 50..250 (bin width=12).
 * Find the 3 highest bins → cluster centers for 2T, 3T, 4T.
 * Set thresholds at midpoints between adjacent centers.
 * Also calibrate the adaptive bump = (3T_center - 2T_center) / 2.
 */
static void mfm_calibrate_thresholds(void) {
    #define CAL_BIN_COUNT  16
    #define CAL_BIN_MIN    50
    #define CAL_BIN_MAX   242   /* 50 + 16*12 = 242 */
    #define CAL_BIN_WIDTH  12

    uint16_t bins[CAL_BIN_COUNT];
    uint16_t count = raw_interval_idx;

    if (count < 30) {
        TRACE("[MFM] cal: too few samples\r\n");
        return;
    }

    /* Clear bins */
    for (uint8_t i = 0; i < CAL_BIN_COUNT; i++) bins[i] = 0;

    /* Build histogram */
    for (uint16_t i = 0; i < count; i++) {
        uint16_t v = raw_intervals[i];
        if (v >= CAL_BIN_MIN && v < CAL_BIN_MAX) {
            uint8_t b = (uint8_t)((v - CAL_BIN_MIN) / CAL_BIN_WIDTH);
            bins[b]++;
        }
    }

    /* Find the 3 highest bins (must be separated by at least 1 bin) */
    uint8_t peak[3] = {0, 0, 0};
    uint16_t peak_val[3] = {0, 0, 0};

    for (uint8_t pass = 0; pass < 3; pass++) {
        uint16_t best = 0;
        uint8_t best_i = 0;
        for (uint8_t i = 0; i < CAL_BIN_COUNT; i++) {
            if (bins[i] > best) {
                /* Check not adjacent to an already-found peak */
                bool too_close = false;
                for (uint8_t p = 0; p < pass; p++) {
                    int8_t diff = (int8_t)i - (int8_t)peak[p];
                    if (diff < 0) diff = -diff;
                    if (diff <= 1) { too_close = true; break; }
                }
                if (!too_close) {
                    best = bins[i];
                    best_i = i;
                }
            }
        }
        peak[pass] = best_i;
        peak_val[pass] = best;
    }

    /* Sort peaks by bin index (ascending → 2T, 3T, 4T) */
    for (uint8_t i = 0; i < 2; i++) {
        for (uint8_t j = i + 1; j < 3; j++) {
            if (peak[j] < peak[i]) {
                uint8_t tmp = peak[i]; peak[i] = peak[j]; peak[j] = tmp;
                uint16_t tv = peak_val[i]; peak_val[i] = peak_val[j]; peak_val[j] = tv;
            }
        }
    }

    /* Validate: need at least some counts in each peak */
    if (peak_val[0] < 3 || peak_val[1] < 3 || peak_val[2] < 3) {
        TRACE("[MFM] cal: weak clusters, keeping defaults\r\n");
        return;
    }

    /* Compute cluster centers (center of each bin) */
    uint16_t center_2t = CAL_BIN_MIN + (uint16_t)peak[0] * CAL_BIN_WIDTH + CAL_BIN_WIDTH / 2;
    uint16_t center_3t = CAL_BIN_MIN + (uint16_t)peak[1] * CAL_BIN_WIDTH + CAL_BIN_WIDTH / 2;
    uint16_t center_4t = CAL_BIN_MIN + (uint16_t)peak[2] * CAL_BIN_WIDTH + CAL_BIN_WIDTH / 2;

    /* Thresholds at midpoints between clusters */
    uint16_t thr_s = (center_2t + center_3t) / 2;
    uint16_t thr_m = (center_3t + center_4t) / 2;
    /* Long threshold: 4T center + half the 3T-4T gap */
    uint16_t thr_l = center_4t + (center_4t - center_3t) / 2;

    /* Adaptive bump: half the gap between 2T and 3T */
    uint16_t bump = (center_3t - center_2t) / 2;
    if (bump < 4) bump = 4;
    if (bump > 40) bump = 40;

    /* Apply calibrated values */
    mfm_thr_short  = thr_s;
    mfm_thr_medium = thr_m;
    mfm_thr_long   = thr_l;
    mfm_adaptive_bump = bump;

    /* Print results */
    TRACE("[MFM] cal: 2T=");
    uart_putdec(center_2t);
    TRACE(" 3T=");
    uart_putdec(center_3t);
    TRACE(" 4T=");
    uart_putdec(center_4t);
    TRACE(" thr=");
    uart_putdec(thr_s);
    TRACE("/");
    uart_putdec(thr_m);
    TRACE(" bump=");
    uart_putdec(bump);
    TRACE("\r\n");
}

int mfm_capture_track(void) {
    TRACE("[MFM] capture start\r\n");

    capture_count = 0;
    pulse_pack = 0;
    pulse_in_pack = 0;
    capture_done = false;
    raw_interval_idx = 0;
    prev_code = 2;  /* Assume long recovery at start */

    sram_begin_seq_write(SRAM_MFM_TRACK);
    TIFR1 |= (1 << ICF1);
    prev_capture = ICR1;
    TIMSK1 |= (1 << ICIE1);
    sei();

    uint16_t timeout = 0;
    while (!capture_done && timeout < 500) {
        _delay_ms(1);
        timeout++;
    }

    TIMSK1 &= ~(1 << ICIE1);
    sram_end_seq();

    TRACE("[MFM] capture: ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" packs, ");
    uart_putdec(timeout);
    TRACE("ms\r\n");

    if (timeout >= 500) {
        TRACE("[MFM] capture TIMEOUT!\r\n");
        return FLOPPY_ERR_TIMEOUT;
    }

    /* Pulse code histogram: count 2T/3T/4T/invalid distribution */
    {
        uint16_t hist[4] = {0, 0, 0, 0};
        sram_begin_seq_read(SRAM_MFM_TRACK);
        uint16_t scan = (capture_count > 500) ? 500 : (uint16_t)capture_count;
        for (uint16_t i = 0; i < scan; i++) {
            uint8_t packed = sram_seq_read_byte();
            for (int p = 3; p >= 0; p--) {
                hist[(packed >> (p * 2)) & 0x03]++;
            }
        }
        sram_end_seq();
        TRACE("[MFM] hist 2T=");
        uart_putdec(hist[0]);
        TRACE(" 3T=");
        uart_putdec(hist[1]);
        TRACE(" 4T=");
        uart_putdec(hist[2]);
        TRACE(" inv=");
        uart_putdec(hist[3]);
        TRACE("\r\n");
    }

    /* Dump raw timer intervals (first 200 pulses) as hex */
    TRACE("[MFM] raw ticks (first ");
    uart_putdec(raw_interval_idx);
    TRACE("):\r\n");
    for (uint16_t i = 0; i < raw_interval_idx; i++) {
        uart_puthex16(raw_intervals[i]);
        uart_putchar(' ');
        if ((i % 12) == 11) TRACE("\r\n");
    }
    TRACE("\r\n");

    /* Calibrate ISR thresholds from raw pulse intervals */
    mfm_calibrate_thresholds();

    return FLOPPY_OK;
}

/* ---- Decode (SRAM → sector data) ---- */

/*
 * MFM raw sync pattern: 0x4489 (A1 with missing clock).
 * Only 0x4489 is reliable — 0x44A9 (normal A1) causes false matches.
 */
#define MFM_RAW_SYNC  0x4489

/*
 * Decode state machine.
 * All data flows through one pulse-processing loop — no separate
 * function calls that could desync the SRAM read position.
 */
typedef enum {
    DEC_SCAN_SYNC,   /* Looking for 0x4489 raw pattern */
    DEC_READ_MARK,   /* Extracting address mark byte after 3x sync */
    DEC_READ_ID,     /* Extracting 4 sector ID bytes */
    DEC_READ_DATA,   /* Extracting 512 data bytes */
} decode_state_t;

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    TRACE("[MFM] decode sector ");
    uart_putdec(sector);
    TRACE(", scan ");
    uart_putdec((uint16_t)capture_count);
    TRACE(" packs\r\n");

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;
    bool found_sector = false;

    decode_state_t state = DEC_SCAN_SYNC;
    uint8_t byte_val = 0;
    uint8_t byte_bits = 0;
    uint8_t field_bytes[4];
    uint8_t field_pos = 0;
    uint16_t data_pos = 0;
    uint32_t pulse_num = 0;
    uint8_t preamble_count = 0;  /* Consecutive 0x5555 detections */
    int8_t sync_dump = -1;       /* >0: dump this many more values after preamble */
    uint8_t sync_dumps_done = 0; /* Limit to first 3 preambles */

    sram_begin_seq_read(SRAM_MFM_TRACK);

    for (uint32_t sram_pos = 0; sram_pos < capture_count; sram_pos++) {
        uint8_t packed = sram_seq_read_byte();

        for (int p = 3; p >= 0; p--) {
            uint8_t code = (packed >> (p * 2)) & 0x03;

            switch (code) {
                case 0: raw_bits = (raw_bits << 2) | 0x01; raw_avail += 2; break;
                case 1: raw_bits = (raw_bits << 3) | 0x01; raw_avail += 3; break;
                case 2: raw_bits = (raw_bits << 4) | 0x01; raw_avail += 4; break;
                default: raw_bits = 0; raw_avail = 0;
                         sync_count = 0; state = DEC_SCAN_SYNC;
                         byte_bits = 0;
                         continue;
            }
            if (raw_avail > 30) raw_avail = 30;
            pulse_num++;

            /* Debug: detect preamble → extract bytes after it */
            {
                uint16_t bottom = (uint16_t)(raw_bits & 0xFFFF);

                if (bottom == 0x5555) {
                    preamble_count++;
                } else {
                    if (preamble_count >= 4 && sync_dumps_done < 2) {
                        /* Preamble just ended — extract next 10 bytes */
                        TRACE("\r\n[MFM] preamble end @");
                        uart_putdec((uint16_t)pulse_num);
                        TRACE(" (len=");
                        uart_putdec(preamble_count);
                        TRACE(")");
                        /* Save position for two-pass extraction */
                        uint32_t save_sram = sram_pos;
                        uint32_t save_raw = raw_bits;

                        for (uint8_t try_off = 0; try_off <= 1; try_off++) {
                            TRACE(" [off");
                            uart_putdec(try_off);
                            TRACE("]:");
                            uint32_t lr = save_raw;
                            uint8_t db = 0, dn = 0, dc = 0, ta = 0;
                            /* Re-read from saved position */
                            sram_end_seq();
                            sram_begin_seq_read(SRAM_MFM_TRACK + save_sram);
                            sram_pos = save_sram;
                            while (dc < 32 && sram_pos < capture_count) {
                                uint8_t pk = sram_seq_read_byte();
                                sram_pos++;
                                for (int pp = 3; pp >= 0 && dc < 32; pp--) {
                                    uint8_t c2 = (pk >> (pp * 2)) & 0x03;
                                    uint8_t a2;
                                    switch (c2) {
                                        case 0: lr = (lr << 2) | 0x01; a2 = 2; break;
                                        case 1: lr = (lr << 3) | 0x01; a2 = 3; break;
                                        case 2: lr = (lr << 4) | 0x01; a2 = 4; break;
                                        default: a2 = 0; break;
                                    }
                                    ta += a2;
                                    while (ta >= 2 && dc < 32) {
                                        ta -= 2;
                                        db = (db << 1) | ((lr >> (ta + try_off)) & 1);
                                        dn += 2;
                                        if (dn >= 16) {
                                            uart_puthex8(db);
                                            uart_putchar(' ');
                                            db = 0; dn = 0; dc++;
                                        }
                                    }
                                }
                            }
                            TRACE("\r\n");
                        }
                        sync_dumps_done++;
                    }
                    preamble_count = 0;
                }
            }

            if (state == DEC_SCAN_SYNC) {
                /*
                 * Brute-force IDAM search: try all 8 even-bit alignments
                 * in raw_bits. Extract 8 data bits (every other bit) at
                 * each alignment. If we get 0xFE → found IDAM.
                 *
                 * Need 16 MFM bits (8 data bits) → check raw_avail >= 16.
                 */
                if (raw_avail >= 18) {
                    for (uint8_t off = 0; off <= 14; off += 2) {
                        uint8_t test = 0;
                        for (uint8_t b = 0; b < 8; b++) {
                            uint8_t pos = off + b * 2;
                            test = (test << 1) | ((raw_bits >> pos) & 1);
                        }
                        if (test == MFM_IDAM) {
                            /* Peek next 4 bytes to validate sector ID */
                            /* Extract T, S, R, N at this alignment */
                            uint8_t id[4];
                            bool valid = true;
                            uint32_t peek_raw = raw_bits;
                            /* We need ~80 more MFM bits for 4 bytes.
                             * For now, just lock alignment and validate
                             * in READ_ID state. */
                            state = DEC_READ_ID;
                            field_pos = 0;
                            byte_val = 0;
                            byte_bits = 0;
                            raw_avail = off;
                            break;
                        }
                    }
                }
            } else {
                /* States DEC_READ_MARK / DEC_READ_ID / DEC_READ_DATA:
                 * Extract data bits from raw MFM stream.
                 * Every 2 raw MFM bits = 1 data bit (clock+data pair).
                 * After sync alignment, bit at raw_avail pos = data bit. */
                while (raw_avail >= 2) {
                    raw_avail -= 2;
                    uint8_t data_bit = (raw_bits >> raw_avail) & 0x01;
                    byte_val = (byte_val << 1) | data_bit;
                    byte_bits += 2;

                    if (byte_bits >= 16) {
                        /* Complete byte extracted */
                        byte_bits = 0;

                        if (state == DEC_READ_MARK) {
                            TRACE("[MFM] mark=0x");
                            uart_puthex8(byte_val);
                            TRACE("\r\n");
                            if (byte_val == MFM_IDAM) {
                                state = DEC_READ_ID;
                                field_pos = 0;
                            } else if (byte_val == MFM_DAM && found_sector) {
                                state = DEC_READ_DATA;
                                data_pos = 0;
                            } else {
                                state = DEC_SCAN_SYNC;
                                sync_count = 0;
                            }
                        } else if (state == DEC_READ_ID) {
                            field_bytes[field_pos++] = byte_val;
                            if (field_pos >= 4) {
                                uint8_t t = field_bytes[0];
                                uint8_t s = field_bytes[1];
                                uint8_t r = field_bytes[2];
                                uint8_t n = field_bytes[3];

                                /* Validate: T=0-79, S=0-1, R=1-18, N=2 */
                                if (t < 80 && s <= 1 && r >= 1 && r <= 18 && n == 2) {
                                    TRACE("[MFM] VALID IDAM: T=");
                                    uart_putdec(t);
                                    TRACE(" S=");
                                    uart_putdec(s);
                                    TRACE(" R=");
                                    uart_putdec(r);
                                    TRACE(" @");
                                    uart_putdec((uint16_t)pulse_num);
                                    TRACE("\r\n");
                                    if (r == sector) {
                                        TRACE("[MFM] SECTOR MATCH!\r\n");
                                        found_sector = true;
                                    }
                                }
                                /* Invalid or non-matching: resume scanning */
                                state = DEC_SCAN_SYNC;
                            }
                        } else if (state == DEC_READ_DATA) {
                            data_out[data_pos++] = byte_val;
                            if (data_pos >= 512) {
                                TRACE("[MFM] decode OK, 512 bytes\r\n");
                                sram_end_seq();
                                return 0;
                            }
                        }
                        byte_val = 0;
                    }
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
    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;

    sram_begin_seq_read(SRAM_MFM_TRACK);
    uint32_t sram_pos = 0;
    uint32_t end_pos = capture_count;

    /* TODO: rewrite with state machine like mfm_decode_sector */
    (void)raw_bits; (void)raw_avail; (void)sync_count;
    (void)sram_pos; (void)end_pos;

    sram_end_seq();
    return 0;
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
 * Wait for one MFM bit cell (2µs = 32 Timer1 ticks at 16MHz).
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
     * 2. Wait through ID CRC (2 bytes) + gap2 (22 bytes) = 768µs
     * 3. Assert /WGATE, disable interrupts
     * 4. Write: 12×0x00 + 3×sync_A1 + DAM + 512×data + CRC + gap3
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
     * = 24 data bytes × 32µs/byte = 768µs */
    _delay_us(768);

    /* Step 3: Assert /WGATE (via shift register) */
    shiftreg_assert_bit(SR_BIT_WGATE);

    cli();

    /* Initialize Timer1 OC1A for precise bit-cell timing */
    write_prev_bit = 0;
    OCR1A = TCNT1 + MFM_TICKS_PER_CELL_HD;
    TIFR1 = (1 << OCF1A);

    /* Step 4: Write data field */
    /* Preamble: 12 × 0x00 */
    for (uint8_t i = 0; i < 12; i++) {
        mfm_write_data_byte(0x00);
    }

    /* Sync: 3 × 0xA1 (with missing clock) */
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

    /* Gap3: 54 × 0x4E (HD standard) */
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
