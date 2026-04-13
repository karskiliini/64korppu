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

/* Raw MFM sync pattern: 0xA1 with missing clock bit */
#define MFM_RAW_SYNC  0x4489

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

/*
 * Analytical calibration: compute delay from histogram peaks,
 * then determine offset by scoring both candidates.
 *
 * The histogram always shows two dominant clusters:
 *   lower = 2T intervals (nominal 64 ticks + delay)
 *   upper = 3T intervals (nominal 96 ticks + delay)
 * Delay = cluster_center - nominal.
 *
 * This replaces brute-force which was defeated by coincidental
 * byte matches at wrong delay values.
 */
static void mfm_calibrate(uint16_t *hist10) {
    /* Step 1: Find two peak bins from histogram (10 bins, 16-tick each, base 48) */
    uint8_t p1 = 0, p2 = 0;
    uint16_t c1 = 0, c2 = 0;
    for (uint8_t b = 0; b < 10; b++) {
        if (hist10[b] > c1) {
            p2 = p1; c2 = c1;
            p1 = b; c1 = hist10[b];
        } else if (hist10[b] > c2) {
            p2 = b; c2 = hist10[b];
        }
    }
    /* Ensure p1 (lower bin) < p2 (upper bin) */
    if (p1 > p2) { uint8_t t = p1; p1 = p2; p2 = t; }

    /* Bin centers: base(48) + bin*16 + 8 */
    uint8_t center_lo = 56 + p1 * 16;  /* ≈ 2T cluster center */
    uint8_t center_hi = 56 + p2 * 16;  /* ≈ 3T cluster center */

    /* Delay = minimum of both estimates, minus margin for 4T.
     *
     * Variable delay: after longer intervals, the signal recovers
     * more → shorter delay. 4T intervals have ~8-12 less delay
     * than 2T/3T. Using the minimum estimate minus 8 ensures
     * 4T intervals (needed for sync 0x4489) are classified as 4T.
     *
     * At delay=32 (typical): 3T/4T boundary = 144 ticks.
     * Actual 4T intervals ~148-160 → correctly classified. */
    int16_t d1 = (int16_t)center_lo - 64;
    int16_t d2 = (int16_t)center_hi - 96;
    int16_t est = (d1 < d2) ? d1 : d2;
    est -= 8;  /* Margin for variable delay at 4T */
    if (est < 0) est = 0;
    uint8_t ana_delay = (uint8_t)est;

    TRACE("[MFM] cal: peaks at ");
    uart_putdec(center_lo);
    TRACE(" and ");
    uart_putdec(center_hi);
    TRACE(", analytical delay=");
    uart_putdec(ana_delay);
    TRACE("\r\n");

    /* Step 2: Determine offset by scoring both candidates.
     * Try offset=0 and offset=1 with the analytical delay,
     * count 0x4E gap fill bytes — the correct offset produces many. */
    uint16_t best_score = 0;
    uint8_t best_offset = 1;

    for (uint8_t offset = 0; offset <= 1; offset++) {
        uint16_t score = 0;
        uint8_t prev = 0xFF;

        sram_begin_seq_read(SRAM_RAW_CAPTURE);

        uint32_t raw_bits = 0;
        uint8_t raw_avail = 0;
        uint8_t dbyte = 0;
        uint8_t dbits = 0;

        for (uint16_t i = 0; i < CAL_SAMPLE_COUNT; i++) {
            uint8_t v = sram_seq_read_byte();
            int16_t adj = (int16_t)v - (int16_t)ana_delay + 16;
            uint8_t cells;
            if (adj < 32) cells = 2;
            else {
                cells = (uint8_t)(adj / 32);
                if (cells > 4) continue;
            }

            raw_bits = (raw_bits << cells) | 1;
            raw_avail += cells;
            if (raw_avail > 30) raw_avail = 30;

            while (raw_avail >= 2) {
                raw_avail -= 2;
                uint8_t data_bit = (raw_bits >> (raw_avail + offset)) & 1;
                dbyte = (dbyte << 1) | data_bit;
                dbits++;
                if (dbits >= 8) {
                    if (dbyte == 0x4E) {
                        score += 10;
                        if (prev == 0x4E) score += 15;
                    }
                    prev = dbyte;
                    dbyte = 0;
                    dbits = 0;
                }
            }
        }
        sram_end_seq();

        TRACE("[MFM]   off=");
        uart_putdec(offset);
        TRACE(" 4E_score=");
        uart_putdec(score);
        TRACE("\r\n");

        if (score > best_score) {
            best_score = score;
            best_offset = offset;
        }
    }

    cal_delay = ana_delay;
    cal_offset = best_offset;

    TRACE("[MFM] cal: delay=");
    uart_putdec(ana_delay);
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

    /* Raw interval histogram: 16-tick bins from 48..207 (10 bins) + min/max */
    uint16_t hist10[10] = {0};
    {
        uint16_t scan = (capture_count > 500) ? 500 : (uint16_t)capture_count;
        uint8_t vmin = 255, vmax = 0;

        sram_begin_seq_read(SRAM_RAW_CAPTURE);
        for (uint16_t i = 0; i < scan; i++) {
            uint8_t v = sram_seq_read_byte();
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            if (v >= 48 && v < 208) {
                uint8_t bin = (v - 48) / 16;
                hist10[bin]++;
            }
        }
        sram_end_seq();

        TRACE("[MFM] range: min=");
        uart_putdec(vmin);
        TRACE(" max=");
        uart_putdec(vmax);
        TRACE("\r\n[MFM] hist:");
        for (uint8_t b = 0; b < 10; b++) {
            if (hist10[b] == 0) continue;
            uart_putchar(' ');
            uart_putdec(48 + (uint16_t)b * 16);
            uart_putchar('-');
            uart_putdec(48 + (uint16_t)(b + 1) * 16 - 1);
            uart_putchar('=');
            uart_putdec(hist10[b]);
        }
        TRACE("\r\n");
    }

    /* Dump first 64 raw tick values for visual inspection */
    {
        sram_begin_seq_read(SRAM_RAW_CAPTURE);
        uint8_t n = (capture_count > 64) ? 64 : (uint8_t)capture_count;
        TRACE("[MFM] raw:");
        for (uint8_t i = 0; i < n; i++) {
            uart_putchar(' ');
            uart_puthex8(sram_seq_read_byte());
        }
        sram_end_seq();
        TRACE("\r\n");
    }

    /* Analytical calibration from histogram peaks */
    mfm_calibrate(hist10);

    /* Full capture diagnostics: cell counts + preamble detection + sync search.
     * Scans ALL captured intervals to understand the track structure. */
    {
        sram_begin_seq_read(SRAM_RAW_CAPTURE);
        uint8_t delay = cal_delay;
        uint16_t cnt_2t = 0, cnt_3t = 0, cnt_4t = 0, cnt_inv = 0;
        uint16_t run_2t = 0;           /* Current consecutive 2T count */
        uint8_t preambles_shown = 0;   /* Max 4 preamble dumps */

        /* After preamble: accumulate raw_bits and look for 0x4489 */
        uint32_t raw_bits = 0;
        uint8_t in_post_preamble = 0;  /* >0: dumping N intervals after preamble */

        for (uint32_t pos = 0; pos < capture_count; pos++) {
            uint8_t v = sram_seq_read_byte();
            int16_t adj = (int16_t)v - (int16_t)delay + 16;
            uint8_t cells;
            if (adj < 32) cells = 2;
            else {
                cells = (uint8_t)(adj / 32);
                if (cells > 4) { cnt_inv++; run_2t = 0; in_post_preamble = 0; raw_bits = 0; continue; }
            }

            if (cells == 2) { cnt_2t++; run_2t++; }
            else if (cells == 3) { cnt_3t++; run_2t = 0; }
            else { cnt_4t++; run_2t = 0; }

            /* Preamble detection: 20+ consecutive 2T intervals */
            if (cells != 2 && run_2t >= 20 && preambles_shown < 4) {
                /* Preamble just ended — dump next intervals */
                TRACE("[MFM] preamble: ");
                uart_putdec(run_2t);
                TRACE("x2T @");
                uart_putdec((uint16_t)pos);
                TRACE(", after:");
                in_post_preamble = 1;
                raw_bits = 0;
                /* Fall through to handle current interval */
            }

            if (in_post_preamble > 0 && in_post_preamble <= 20) {
                uart_putchar(' ');
                uart_puthex8(v);
                uart_putchar('(');
                uart_putchar('0' + cells);
                uart_putchar(')');

                /* Also accumulate raw_bits for sync check */
                raw_bits = (raw_bits << cells) | 1;
                if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
                    TRACE(" *SYNC*");
                }

                in_post_preamble++;
                if (in_post_preamble > 20) {
                    TRACE("\r\n");
                    preambles_shown++;
                    in_post_preamble = 0;
                }
            }

            /* Reset run_2t AFTER preamble check (cells != 2 case handled above) */
        }
        sram_end_seq();

        TRACE("[MFM] cells: 2T=");
        uart_putdec(cnt_2t);
        TRACE(" 3T=");
        uart_putdec(cnt_3t);
        TRACE(" 4T=");
        uart_putdec(cnt_4t);
        TRACE(" inv=");
        uart_putdec(cnt_inv);
        TRACE("\r\n");
    }

    return FLOPPY_OK;
}

/* ---- Decode (SRAM → sector data) ---- */

/*
 * Decode state machine using raw 0x4489 sync pattern detection.
 *
 * Real floppy controllers use a PLL + sync detector, not byte-level
 * matching. The 0xA1 sync byte has a MISSING CLOCK that produces
 * the unique raw MFM pattern 0x4489. This is self-aligning: once
 * found, the bit framing is known (offset=0 after sync).
 *
 * States:
 *   SCAN_SYNC: look for (raw_bits & 0xFFFF) == 0x4489, count 3
 *   READ_MARK: extract one byte (IDAM 0xFE or DAM 0xFB)
 *   READ_ID:   extract 4 sector ID bytes (T, S, R, N)
 *   READ_DATA: extract 512 data bytes
 */
typedef enum {
    DEC_SCAN_SYNC,
    DEC_READ_MARK,
    DEC_READ_ID,
    DEC_READ_DATA,
} decode_state_t;

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    TRACE("[MFM] decode sector ");
    uart_putdec(sector);
    TRACE(" delay=");
    uart_putdec(cal_delay);
    TRACE("\r\n");

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    bool found_sector = false;

    decode_state_t state = DEC_SCAN_SYNC;
    uint8_t sync_count = 0;
    uint8_t byte_val = 0;
    uint8_t byte_bits = 0;
    uint8_t field_bytes[4];
    uint8_t field_pos = 0;
    uint16_t data_pos = 0;
    uint8_t delay = cal_delay;

    sram_begin_seq_read(SRAM_RAW_CAPTURE);

    for (uint32_t pos = 0; pos < capture_count; pos++) {
        uint8_t interval = sram_seq_read_byte();

        /* Classify interval using calibrated delay */
        int16_t adj = (int16_t)interval - (int16_t)delay + 16;
        uint8_t cells;
        if (adj < 32) {
            cells = 2;
        } else {
            cells = (uint8_t)(adj / 32);
            if (cells > 4) {
                raw_bits = 0; raw_avail = 0;
                sync_count = 0;
                if (state != DEC_SCAN_SYNC) {
                    state = DEC_SCAN_SYNC;
                    byte_bits = 0;
                }
                continue;
            }
        }

        /* Add to MFM bitstream */
        raw_bits = (raw_bits << cells) | 1;
        raw_avail += cells;
        if (raw_avail > 30) raw_avail = 30;

        if (state == DEC_SCAN_SYNC) {
            /*
             * Look for raw MFM sync pattern 0x4489.
             * Need 3 consecutive syncs before address mark.
             * This is self-aligning — no offset needed.
             */
            if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
                sync_count++;
                if (sync_count >= 3) {
                    /* 3 sync marks found — next byte is address mark.
                     * Reset bit extraction: offset=0 after sync. */
                    state = DEC_READ_MARK;
                    byte_val = 0;
                    byte_bits = 0;
                    raw_avail = 0;
                }
            } else if (sync_count > 0 && (raw_bits & 0xFFFF) != MFM_RAW_SYNC) {
                /* Lost sync — only reset if we haven't reached 3 yet */
                if (sync_count < 3) sync_count = 0;
            }
        } else {
            /* States READ_MARK / READ_ID / READ_DATA:
             * Extract data bits. After sync, offset=0 gives data bits. */
            while (raw_avail >= 2) {
                raw_avail -= 2;
                uint8_t data_bit = (raw_bits >> raw_avail) & 0x01;
                byte_val = (byte_val << 1) | data_bit;
                byte_bits++;

                if (byte_bits >= 8) {
                    byte_bits = 0;

                    if (state == DEC_READ_MARK) {
                        /* Address mark byte after sync */
                        if (!found_sector && byte_val == MFM_IDAM) {
                            state = DEC_READ_ID;
                            field_pos = 0;
                        } else if (found_sector && byte_val == MFM_DAM) {
                            state = DEC_READ_DATA;
                            data_pos = 0;
                        } else {
                            /* Unexpected mark — back to scanning */
                            state = DEC_SCAN_SYNC;
                            sync_count = 0;
                            break;
                        }
                    } else if (state == DEC_READ_ID) {
                        field_bytes[field_pos++] = byte_val;
                        if (field_pos >= 4) {
                            uint8_t t = field_bytes[0];
                            uint8_t s = field_bytes[1];
                            uint8_t r = field_bytes[2];
                            uint8_t n = field_bytes[3];

                            if (t < 80 && s <= 1 && r >= 1 && r <= 18 && n == 2) {
                                TRACE("[MFM] IDAM T=");
                                uart_putdec(t);
                                TRACE(" S=");
                                uart_putdec(s);
                                TRACE(" R=");
                                uart_putdec(r);
                                TRACE("\r\n");
                                if (r == sector) {
                                    found_sector = true;
                                }
                            }
                            state = DEC_SCAN_SYNC;
                            sync_count = 0;
                            break;
                        }
                    } else if (state == DEC_READ_DATA) {
                        data_out[data_pos++] = byte_val;
                        if (data_pos >= 512) {
                            TRACE("[MFM] decode OK\r\n");
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
