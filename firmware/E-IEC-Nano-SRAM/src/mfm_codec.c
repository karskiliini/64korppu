#include "mfm_codec.h"
#include "config.h"
#include "sram.h"
#include "shiftreg.h"
#include "uart.h"

#ifdef __AVR__
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#endif
#include <string.h>

/*
 * File layout: the pure decode/calibration logic below compiles on
 * BOTH AVR and host (unit tests exercise the exact same code paths);
 * the capture and write sections at the bottom are AVR-only.
 * Host builds provide mock sram_* functions (see test/mock_sram.c).
 */

/*
 * MFM codec for ATmega328P — raw interval architecture.
 *
 * Read:   Polled Timer1 ICP1 (D8) capture. Flux-transition intervals
 *         stored as uint8_t timer ticks sequentially into external SRAM.
 *
 *         Polling (interrupts disabled), NOT an ISR: HD 500kbps
 *         intervals are 2/3/4µs = 32/48/64 ticks @16MHz, but a C ISR
 *         with an SPI store costs ~6-7µs. The old ISR merged pulses
 *         and produced artefact clusters (~107/133/155 ticks) that
 *         tracked the ISR service time, not the disk signal — all
 *         "pull-up delay" calibration was fitting that artefact.
 *         The polled loop is ~25-30 cycles (<2µs) per edge, and ICR1
 *         hardware latching gives one extra interval of slack.
 *
 * Decode: Read intervals back from SRAM, calibrate 2T/3T/4T thresholds
 *         from the interval histogram, decode via raw 0x4489 sync
 *         detection, verify ID and data field CRCs.
 *
 * Write:  DISABLED (MFM_WRITE_ENABLED=0) — see mfm_write_sector.
 */

/* Raw MFM sync pattern: 0xA1 with missing clock bit */
#define MFM_RAW_SYNC  0x4489

/*
 * Write path gate. A C loop cannot hold the HD 1µs half-cell grid at
 * 16MHz (16 cycles/half-cell budget vs ~21-28 cycles of loop work), so
 * bit-banged writes would land on a wrong bit rate and corrupt the
 * disk. Kept disabled until the emitter is rewritten as cycle-balanced
 * interleaved assembler (generate byte N+1 inside byte N's pad slots).
 * DD 250kbps (32-tick half-cell) WOULD fit a C loop, if a DD mode is
 * added later.
 */
#define MFM_WRITE_ENABLED 0

/* ---- Capture state ---- */

static uint16_t capture_count;

/* Test/host hooks: the capture loop sets this on AVR; host tests fill
 * the mock SRAM directly and set the count through these. */
void mfm_set_capture_count(uint16_t n) { capture_count = n; }
uint16_t mfm_get_capture_count(void) { return capture_count; }

/* ---- Calibration results (set after capture, used by decoder) ---- */

static uint8_t cal_thr_23;    /* interval < thr_23  → 2T */
static uint8_t cal_thr_34;    /* interval < thr_34  → 3T */
static uint8_t cal_thr_4max;  /* interval < thr_4max → 4T, else invalid */

/* ---- Software PLL: adaptive half-cell period tracking ---- */

/*
 * Real FDCs track the bit-cell grid with a hardware PLL. We have no
 * PLL in hardware, but because decode runs OFFLINE on raw captured
 * intervals, the PLL can live entirely in software with no real-time
 * cost: a first-order period tracker, the same scheme flux tools like
 * Greaseweazle/FluxEngine use.
 *
 * The half-cell period T is kept in 8.8 fixed point (nominal 1µs =
 * 16 ticks = 0x1000). For each interval d:
 *
 *   cells = nearest of 2/3/4  (boundaries at 1.5T / 2.5T / 3.5T / 4.5T)
 *   error = d - cells*T       (per-transition timing error)
 *   T    += (error/cells) >> PLL_GAIN_SHIFT
 *
 * Effective loop gain ≈ 1/16 per cell — fast enough to track motor
 * speed drift and once-per-revolution wobble (eccentric clamping),
 * slow enough not to chase bit-shift jitter. T is clamped to ±~20% of
 * nominal so a burst of noise cannot run the tracker away, and the
 * period is re-seeded from the histogram calibration at the start of
 * every decode pass.
 *
 * This tracks frequency, not absolute phase — quantizing each
 * interval to the nearest cell count IS the phase decision, so a
 * separate phase accumulator is unnecessary at these SNR levels.
 */
#define PLL_T_NOMINAL   ((uint16_t)16 << 8)
#define PLL_T_MIN       ((uint16_t)13 << 8)
#define PLL_T_MAX       ((uint16_t)19 << 8)
#define PLL_GAIN_SHIFT  4

static uint16_t pll_seed = PLL_T_NOMINAL;   /* set by calibration */
static uint16_t pll_period = PLL_T_NOMINAL; /* live tracker state */

void mfm_pll_reset(void) { pll_period = pll_seed; }
uint16_t mfm_pll_period(void) { return pll_period; }

/* Classify one interval against the tracked grid and update the
 * tracker. Returns 2, 3, 4 (valid) or 0 (invalid/noise — tracker is
 * left untouched so glitches cannot disturb it). */
static inline uint8_t mfm_classify(uint8_t interval) {
    uint16_t d = (uint16_t)interval << 8;
    uint16_t T = pll_period;

    uint8_t cells;
    if (d < (uint16_t)(T + (T >> 1))) return 0;            /* < 1.5T */
    if      (d < (uint16_t)(2 * T + (T >> 1))) cells = 2;  /* < 2.5T */
    else if (d < (uint16_t)(3 * T + (T >> 1))) cells = 3;  /* < 3.5T */
    else if (d < (uint16_t)(4 * T + (T >> 1))) cells = 4;  /* < 4.5T */
    else return 0;                                         /* ≥ 4.5T */

    /* error / cells via cheap shift approximations:
     * /2 exact, /3 ≈ 0.3125, /4 exact. Arithmetic >> on negative
     * int16 is well-defined (arithmetic shift) on avr-gcc/gcc. */
    int16_t err = (int16_t)(d - cells * T);
    int16_t adj;
    if (cells == 2)      adj = err >> 1;
    else if (cells == 3) adj = (err >> 2) + (err >> 4);
    else                 adj = err >> 2;

    T = (uint16_t)((int16_t)T + (adj >> PLL_GAIN_SHIFT));
    if (T < PLL_T_MIN) T = PLL_T_MIN;
    if (T > PLL_T_MAX) T = PLL_T_MAX;
    pll_period = T;

    return cells;
}

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

/* CRC seed over the 3x 0xA1 sync bytes + address mark byte */
static uint16_t mfm_crc_sync_mark(uint8_t mark) {
    uint8_t hdr[4] = { MFM_SYNC_BYTE, MFM_SYNC_BYTE, MFM_SYNC_BYTE, mark };
    return mfm_crc16(hdr, 4, MFM_CRC_INIT);
}

/* ---- Threshold defaults (common) ---- */

/* Default calibration thresholds (nominal HD 32/48/64 ticks),
 * overwritten from the histogram after the first capture. */
void mfm_set_default_thresholds(void) {
    cal_thr_23   = MFM_THRESHOLD_SHORT;   /* 40 */
    cal_thr_34   = MFM_THRESHOLD_MEDIUM;  /* 56 */
    cal_thr_4max = MFM_THRESHOLD_LONG;    /* 76 */
    pll_seed = PLL_T_NOMINAL;
    pll_period = PLL_T_NOMINAL;
}

/* ---- Calibration ---- */

/*
 * Analytical calibration: find the 2T and 3T cluster peaks from the
 * interval histogram and place classification thresholds at the
 * cluster midpoints.
 *
 * Histogram: 10 bins x 8 ticks from 28 to 107. The base of 28 centers
 * the nominal HD clusters (2T=32, 3T=48, 4T=64) INSIDE bins 0/2/4 —
 * with a base of 24 the cluster centers would sit exactly on bin
 * boundaries and timing jitter would split a cluster across two bins,
 * skewing the peak search.
 *
 * The second peak is required to be at least 2 bins away from the
 * first: adjacent-bin spill-over from the same cluster must not be
 * mistaken for the other cluster.
 */
static void mfm_calibrate(uint16_t *hist10) {
    uint8_t p1 = 0, p2 = 0;
    uint16_t c1 = 0, c2 = 0;
    for (uint8_t b = 0; b < 10; b++) {
        if (hist10[b] > c1) { p1 = b; c1 = hist10[b]; }
    }
    for (uint8_t b = 0; b < 10; b++) {
        uint8_t dist = (b > p1) ? (b - p1) : (p1 - b);
        if (dist >= 2 && hist10[b] > c2) { p2 = b; c2 = hist10[b]; }
    }
    /* Ensure p1 (lower bin) < p2 (upper bin) */
    if (p1 > p2) { uint8_t t = p1; p1 = p2; p2 = t; }

    /* Bin centers: base(28) + bin*8 + 4 */
    uint8_t center_lo = 32 + p1 * 8;  /* ≈ 2T cluster center (~32) */
    uint8_t center_hi = 32 + p2 * 8;  /* ≈ 3T cluster center (~48) */
    uint8_t spacing = center_hi - center_lo;

    if (c2 == 0 || spacing < 12 || spacing > 24) {
        /* Degenerate histogram (bad capture) — keep current thresholds */
        TRACE("[MFM] cal: degenerate peaks ");
        uart_putdec(center_lo);
        uart_putchar('/');
        uart_putdec(center_hi);
        TRACE(", keeping thresholds\r\n");
        return;
    }

    cal_thr_23   = (uint8_t)((center_lo + center_hi) / 2);   /* ~40 */
    cal_thr_34   = (uint8_t)(center_hi + spacing / 2);       /* ~56 */
    cal_thr_4max = (uint8_t)(cal_thr_34 + spacing);          /* ~72 */

    /* Seed the software PLL: 2T cluster center / 2 = half-cell T,
     * in 8.8 fixed point (<<8 then /2 = <<7). */
    pll_seed = (uint16_t)center_lo << 7;
    pll_period = pll_seed;

    TRACE("[MFM] cal: peaks ");
    uart_putdec(center_lo);
    uart_putchar('/');
    uart_putdec(center_hi);
    TRACE(" thr=");
    uart_putdec(cal_thr_23);
    uart_putchar('/');
    uart_putdec(cal_thr_34);
    uart_putchar('/');
    uart_putdec(cal_thr_4max);
    TRACE("\r\n");
}

/*
 * Build the interval histogram from the captured raw data in SRAM and
 * recalibrate the classification thresholds. Common code: called by
 * mfm_capture_track on AVR and directly by host tests.
 */
void mfm_calibrate_capture(void) {
    uint16_t hist10[10] = {0};
    uint16_t scan = (capture_count > 2000) ? 2000 : capture_count;
    uint8_t vmin = 255, vmax = 0;

    sram_begin_seq_read(SRAM_RAW_CAPTURE);
    for (uint16_t i = 0; i < scan; i++) {
        uint8_t v = sram_seq_read_byte();
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        if (v >= 28 && v < 108) {
            uint8_t bin = (v - 28) / 8;
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
        uart_putdec(28 + (uint16_t)b * 8);
        uart_putchar('-');
        uart_putdec(28 + (uint16_t)(b + 1) * 8 - 1);
        uart_putchar('=');
        uart_putdec(hist10[b]);
    }
    TRACE("\r\n");

    mfm_calibrate(hist10);
}

/* ================= AVR-only: init + capture ================= */

#ifdef __AVR__

/* ---- Init ---- */

void mfm_init(void) {
    /* Timer1: Normal mode, no prescaler (16MHz clock, 62.5ns/tick) */
    TCCR1A = 0;
    TCCR1B = (1 << CS10);  /* No prescaler */

    /*
     * ICP1 input on PB0 (D8), falling edge trigger.
     * The PCB routes /RDATA straight to D8 (active-low pulses), so the
     * falling edge is the drive's sharp edge. NOTE: if a 74LS14/74HC14
     * inverter is added in front of D8, switch to rising edge:
     * TCCR1B |= (1 << ICES1);
     */
    TCCR1B &= ~(1 << ICES1);
    /* ICNC1 disabled — the noise canceler's 4-tick fixed delay would
     * cancel in the interval difference anyway. */

    /* Capture interrupt permanently disabled — capture is polled */
    TIMSK1 &= ~(1 << ICIE1);

    mfm_set_default_thresholds();
}

/* ---- Capture track ---- */

int mfm_capture_track(void) {
    /* Clamp capture size to the actual SRAM chip (23LC256 = 32KB!) */
    uint16_t cap = SRAM_RAW_CAPTURE_SIZE;
    if (sram_capacity() < (uint32_t)cap) {
        cap = (uint16_t)sram_capacity();
    }

    TRACE("[MFM] capture start (polled, cap=");
    uart_putdec(cap);
    TRACE(")\r\n");

    capture_count = 0;

    /* Wait for the first flux transition, up to ~500ms */
    TIFR1 = (1 << ICF1);
    {
        uint16_t waited = 0;
        while (!(TIFR1 & (1 << ICF1))) {
            _delay_us(50);
            if (++waited > 10000) {
                TRACE("[MFM] capture TIMEOUT (no /RDATA edges)\r\n");
                return FLOPPY_ERR_TIMEOUT;
            }
        }
    }

    sram_begin_seq_write(SRAM_RAW_CAPTURE);

    /*
     * Polled capture, interrupts off (~150ms for a full buffer).
     *
     * Per-edge budget is 32 cycles (HD 2T = 2µs). The loop does:
     * ICF1 poll exit + ICR1L read + flag clear + 8-bit delta + SPSR
     * interlock + SPDR write + count/compare ≈ 25-30 cycles.
     *
     * 8-bit ICR1L is enough: valid intervals are < 108 ticks. The
     * SPSR poll doubles as the SPI interlock: SPIF sets when the
     * previous byte has left the shift register, and the SPSR-read +
     * SPDR-write sequence clears it. SPI @8MHz takes 16 cycles/byte,
     * the loop is longer, so SPI never stalls us in steady state.
     *
     * ATN (INT0) stays pending during capture and is serviced right
     * after sei() — same availability as the old saturated-ISR code.
     */
    uint16_t count = 0;
    bool first = true;

    cli();
    uint8_t prev8 = ICR1L;
    TIFR1 = (1 << ICF1);

    while (count < cap) {
        uint16_t idle = 0;
        while (!(TIFR1 & (1 << ICF1))) {
            if (++idle >= 20000) goto capture_end;  /* ~5ms silence */
        }
        uint8_t now8 = ICR1L;
        TIFR1 = (1 << ICF1);
        uint8_t interval = now8 - prev8;
        prev8 = now8;
        if (!first) {
            while (!(SPSR & (1 << SPIF)));
        }
        first = false;
        SPDR = interval;
        count++;
    }
capture_end:
    if (!first) {
        while (!(SPSR & (1 << SPIF)));  /* drain the final SPI byte */
        (void)SPDR;
    }
    sei();
    sram_end_seq();

    capture_count = count;

    TRACE("[MFM] capture: ");
    uart_putdec(capture_count);
    TRACE(" intervals\r\n");

    if (capture_count < 1000) {
        TRACE("[MFM] capture too short\r\n");
        return FLOPPY_ERR_TIMEOUT;
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

    /* Histogram + threshold calibration (common code) */
    mfm_calibrate_capture();

    return FLOPPY_OK;
}

#endif /* __AVR__ (init + capture) */

/* ---- Decode (SRAM → sector data) ---- */

/*
 * Decode state machine using raw 0x4489 sync pattern detection.
 *
 * The 0xA1 sync byte with a MISSING CLOCK produces the unique raw MFM
 * pattern 0x4489 which cannot occur in normally-encoded data. It is
 * self-aligning: once matched, bit framing is known (offset 0).
 *
 * Sync counting: consecutive 0x4489 matches are 16 raw bits apart, so
 * the window only matches at sync-byte boundaries. Non-matching
 * windows BETWEEN boundaries are normal and must NOT reset the count
 * (the old code did, which made 3 consecutive syncs unreachable and
 * decode always fail — verified against synthetic perfect data).
 * A reset only happens on an invalid interval.
 *
 * Field layout after 3x sync:
 *   IDAM 0xFE + T,S,R,N + CRC16(hi,lo)   — CRC over A1 A1 A1 FE T S R N
 *   DAM  0xFB + 512 data + CRC16(hi,lo)  — CRC over A1 A1 A1 FB data...
 */
typedef enum {
    DEC_SCAN_SYNC,
    DEC_READ_MARK,
    DEC_READ_ID,
    DEC_READ_DATA,
} decode_state_t;

int mfm_decode_sector(uint8_t sector, uint8_t *data_out) {
    mfm_pll_reset();

    TRACE("[MFM] decode R=");
    uart_putdec(sector);
    TRACE(" T=");
    uart_putdec(pll_period >> 8);
    uart_putchar('.');
    uart_putdec((uint16_t)(pll_period & 0xFF) * 100 / 256);
    TRACE(" (PLL)\r\n");

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    bool found_sector = false;

    decode_state_t state = DEC_SCAN_SYNC;
    uint8_t sync_count = 0;
    uint8_t byte_val = 0;
    uint8_t byte_bits = 0;
    uint8_t field_bytes[6];      /* T,S,R,N + CRC hi,lo */
    uint8_t field_pos = 0;
    uint16_t data_pos = 0;
    uint16_t crc = 0;
    uint8_t crc_hi = 0;

    sram_begin_seq_read(SRAM_RAW_CAPTURE);

    for (uint16_t pos = 0; pos < capture_count; pos++) {
        uint8_t interval = sram_seq_read_byte();

        uint8_t cells = mfm_classify(interval);
        if (cells == 0) {
            raw_bits = 0; raw_avail = 0;
            sync_count = 0;
            if (state != DEC_SCAN_SYNC) {
                state = DEC_SCAN_SYNC;
                byte_bits = 0;
            }
            continue;
        }

        raw_bits = (raw_bits << cells) | 1;
        raw_avail += cells;
        if (raw_avail > 30) raw_avail = 30;

        if (state == DEC_SCAN_SYNC) {
            if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
                sync_count++;
                if (sync_count >= 3) {
                    /* 3 sync marks — next byte is the address mark.
                     * Reset bit extraction: offset 0 after sync. */
                    state = DEC_READ_MARK;
                    byte_val = 0;
                    byte_bits = 0;
                    raw_avail = 0;
                }
            }
            /* No reset on non-matching windows: matches only occur at
             * 16-bit sync boundaries, everything in between is normal. */
        } else {
            /* Extract data bits (every 2nd raw bit after sync) */
            while (raw_avail >= 2) {
                raw_avail -= 2;
                uint8_t data_bit = (raw_bits >> raw_avail) & 0x01;
                byte_val = (byte_val << 1) | data_bit;
                byte_bits++;

                if (byte_bits >= 8) {
                    byte_bits = 0;

                    if (state == DEC_READ_MARK) {
                        if (!found_sector && byte_val == MFM_IDAM) {
                            state = DEC_READ_ID;
                            field_pos = 0;
                            crc = mfm_crc_sync_mark(MFM_IDAM);
                        } else if (found_sector && byte_val == MFM_DAM) {
                            state = DEC_READ_DATA;
                            data_pos = 0;
                            crc = mfm_crc_sync_mark(MFM_DAM);
                        } else {
                            state = DEC_SCAN_SYNC;
                            sync_count = 0;
                            break;
                        }
                    } else if (state == DEC_READ_ID) {
                        field_bytes[field_pos++] = byte_val;
                        if (field_pos >= 6) {
                            crc = mfm_crc16(field_bytes, 4, crc);
                            uint16_t stored = ((uint16_t)field_bytes[4] << 8)
                                              | field_bytes[5];
                            uint8_t t = field_bytes[0];
                            uint8_t s = field_bytes[1];
                            uint8_t r = field_bytes[2];
                            uint8_t n = field_bytes[3];

                            if (crc == stored &&
                                t < 80 && s <= 1 && r >= 1 && r <= 18 &&
                                n == 2) {
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
                        if (data_pos < 512) {
                            data_out[data_pos] = byte_val;
                            crc = mfm_crc16(&byte_val, 1, crc);
                        } else if (data_pos == 512) {
                            crc_hi = byte_val;
                        } else {
                            uint16_t stored = ((uint16_t)crc_hi << 8)
                                              | byte_val;
                            sram_end_seq();
                            if (crc == stored) {
                                TRACE("[MFM] decode OK\r\n");
                                return FLOPPY_OK;
                            }
                            TRACE("[MFM] data CRC FAIL\r\n");
                            return FLOPPY_ERR_CRC;
                        }
                        data_pos++;
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

/* ================= AVR-only: write ================= */

#ifdef __AVR__

/* ---- Write sector ---- */

#if MFM_WRITE_ENABLED

/* Write-side polling thresholds (16-bit ICR1 deltas) */
static uint16_t mfm_thr_short  = MFM_THRESHOLD_SHORT;
static uint16_t mfm_thr_medium = MFM_THRESHOLD_MEDIUM;
static uint16_t mfm_thr_long   = MFM_THRESHOLD_LONG;

static uint16_t prev_capture;
static uint8_t write_prev_bit;

/*
 * /WDATA pulse: short negative pulse (~300ns).
 * Drive detects falling edge as flux transition.
 */
static inline void wdata_pulse(void) {
    FLOPPY_WDATA_PORT &= ~(1 << FLOPPY_WDATA_PIN);
    __builtin_avr_delay_cycles(5);
    FLOPPY_WDATA_PORT |= (1 << FLOPPY_WDATA_PIN);
}

/*
 * Wait for one MFM half-cell (HD: 1µs = 16 Timer1 ticks at 16MHz).
 * Uses OC1A compare match on an absolute grid.
 */
static inline void wait_bit_cell(void) {
    while (!(TIFR1 & (1 << OCF1A)));
    TIFR1 = (1 << OCF1A);
    OCR1A += MFM_TICKS_PER_CELL_HD;
}

static void mfm_write_data_byte(uint8_t byte) {
    for (int8_t i = 7; i >= 0; i--) {
        uint8_t data_bit = (byte >> i) & 1;

        /* Clock half-cell */
        wait_bit_cell();
        if (!write_prev_bit && !data_bit) {
            wdata_pulse();
        }

        /* Data half-cell */
        wait_bit_cell();
        if (data_bit) {
            wdata_pulse();
        }

        write_prev_bit = data_bit;
    }
}

static void mfm_write_sync_a1(void) {
    uint16_t pattern = 0x4489;
    for (int8_t i = 15; i >= 0; i--) {
        wait_bit_cell();
        if (pattern & (1U << i)) {
            wdata_pulse();
        }
    }
    write_prev_bit = 1;
}

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

static int mfm_wait_for_sector(uint8_t target_sector) {
    TRACE("[MFM] scan for sector ");
    uart_putdec(target_sector);
    TRACE("...\r\n");

    prev_capture = ICR1;

    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;

    /* Scan up to ~2 revolutions (HD: ~74000 pulses/rev) */
    for (uint32_t n = 0; n < 160000UL; n++) {
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

#endif /* MFM_WRITE_ENABLED */

int mfm_write_sector(const mfm_sector_id_t *id, const uint8_t *data) {
#if !MFM_WRITE_ENABLED
    /*
     * HD write is disabled: the C bit-bang loop cannot hold the 1µs
     * half-cell grid at 16MHz (budget 16 cycles, loop needs ~21-28).
     * Writing at a wrong bit rate would trash the sector AND its
     * neighbours (gap overrun), so refusing is the safe behaviour.
     * See the MFM_WRITE_ENABLED comment at the top of this file.
     */
    (void)id;
    (void)data;
    TRACE("[MFM] write DISABLED (HD timing needs asm emitter)\r\n");
    return FLOPPY_ERR_WRITE;
#else
    /*
     * Write data field of an existing sector:
     * 1. Poll /RDATA to find target IDAM in real-time
     * 2. Wait through ID CRC (2 bytes) + gap2 (22 bytes)
     *    = 24 bytes x 16µs/byte = 384µs at HD 500kbps
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

    int rc = mfm_wait_for_sector(id->sector);
    if (rc != FLOPPY_OK) return rc;

    /* ID CRC (2 bytes) + gap2 (22 bytes) = 24 bytes x 16µs at HD */
    _delay_us(384);

    shiftreg_assert_bit(SR_BIT_WGATE);

    cli();

    write_prev_bit = 0;
    OCR1A = TCNT1 + MFM_TICKS_PER_CELL_HD;
    TIFR1 = (1 << OCF1A);

    for (uint8_t i = 0; i < 12; i++) {
        mfm_write_data_byte(0x00);
    }

    mfm_write_sync_a1();
    mfm_write_sync_a1();
    mfm_write_sync_a1();

    mfm_write_data_byte(MFM_DAM);

    uint16_t crc = mfm_crc_sync_mark(MFM_DAM);

    for (uint16_t i = 0; i < 512; i++) {
        mfm_write_data_byte(data[i]);
        crc = mfm_crc16(&data[i], 1, crc);
    }

    uint8_t crc_hi = (crc >> 8) & 0xFF;
    uint8_t crc_lo = crc & 0xFF;
    mfm_write_data_byte(crc_hi);
    mfm_write_data_byte(crc_lo);

    /* Gap3: 54 x 0x4E (HD standard) */
    for (uint8_t i = 0; i < 54; i++) {
        mfm_write_data_byte(0x4E);
    }

    sei();

    shiftreg_release_bit(SR_BIT_WGATE);

    TRACE("[MFM] write OK\r\n");
    return FLOPPY_OK;
#endif
}

#endif /* __AVR__ */
