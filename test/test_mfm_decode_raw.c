/*
 * Offline test: adaptive MFM decoding with linear delay model.
 * delay(prev) = (420 - prev) / 3
 * cells = (interval - delay + 16) / 32
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Trace data from earlier captures */
static const uint8_t trace_clean[] = {
    /* PULLUP_DELAY=66 trace that found preamble */
    0x36, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1, 0xBA, 0x86, 0x85, 0x85,
    0xD6, 0xA0, 0x86, 0x85, 0xBB, 0xA0, 0xA0, 0xA1, 0xD6, 0xA0, 0x86, 0x86,
    0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0x86, 0xBC, 0xA0, 0x86, 0x86,
    0xBC, 0x9F, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0x9F, 0xD7, 0xA0, 0x86, 0x86,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBB, 0x86, 0x85, 0x86, 0xBA, 0x85, 0x85, 0x85,
    0xD7, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1, 0xBB, 0xA0, 0x86, 0x86,
    0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x85, 0xA0, 0xD6, 0xA0, 0xA1, 0x86,
    0xBB, 0xA0, 0xA0, 0xA1, 0xBB, 0xA1, 0x85, 0x85, 0xBC, 0xA0, 0xA1, 0xA0,
    0xBC, 0x86, 0x85, 0xA0, 0xD6, 0xA1, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1,
    0xBB, 0xA0, 0x86, 0x85, 0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0xA1,
    0xD6, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0xA0, 0x86, 0x85,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBC, 0x86, 0x85, 0xA0, 0xD6, 0xA1, 0xA0, 0x86,
    0xBB, 0xA0, 0xA0, 0xA1, 0xD5, 0xA1, 0x86, 0x85, 0xBB, 0xA1, 0xA0, 0xA0,
    0xBB, 0x85, 0x86, 0x86, 0xBB, 0x85, 0x86, 0x86, 0xD6, 0xA0, 0x86, 0x86,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBC, 0x86, 0x85, 0x86, 0xBB, 0x85, 0x86, 0x86,
    0xD5, 0xA1, 0xA0, 0x86, 0xBB, 0xA0, 0xA1, 0xA1, 0xBB, 0xA0, 0x86, 0x87,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBB, 0x85, 0x86, 0xA1,
};

static const uint8_t trace_attempt4[] = {
    0x35, 0xA2, 0xBC, 0xBC, 0xF3, 0xBC, 0xBD, 0xBC, 0xD7, 0xBC, 0xA1, 0xA2,
    0xD8, 0xBC, 0xA2, 0xBD, 0xF2, 0xBC, 0xBD, 0xBD, 0xF2, 0xBD, 0xA1, 0xA2,
    0xD7, 0xBD, 0xA2, 0xBC, 0xF3, 0xBC, 0xBC, 0xBE, 0xF2, 0xBD, 0xBC, 0xBD,
    0xD8, 0xBC, 0xA2, 0xA1, 0xD8, 0xBD, 0xA1, 0xBD, 0xF3, 0xBC, 0xBD, 0xBD,
    0xF2, 0xBD, 0xA2, 0xA1, 0xD8, 0xBD, 0xA2, 0xBC, 0xF3, 0xBD, 0xBC, 0xBD,
    0xF3, 0xBD, 0xA2, 0xA1, 0xD8, 0xBD, 0xA2, 0xBC, 0xF3, 0xBD, 0xBD, 0xBD,
    0xF2, 0xBD, 0xA2, 0xA2, 0xD8, 0xBD, 0xA1, 0xBD, 0xF3, 0xBD, 0xBC, 0xBE,
    0xD8, 0xBD, 0xBC, 0xBD, 0xD8, 0xBD, 0xA2, 0xA2, 0xD8, 0xBD, 0xBD, 0xBD,
    0xF3, 0xBD, 0xBD, 0xBD, 0xF3, 0xBD, 0xA2, 0xA3, 0xD8, 0xBD, 0xBD, 0xBD,
    0xF3, 0xBD, 0xBD, 0xBE, 0xD8, 0xBD, 0xBD, 0xBE, 0xD8, 0xA2, 0xA2, 0xA2,
    0xF4, 0xBD, 0xBD, 0xBD, 0xD8, 0xBD, 0xA2, 0xA2, 0xD8, 0xBE, 0xBD, 0xBD,
    0xF3, 0xBE, 0xBD, 0xBD, 0xD9, 0xBE, 0xA2, 0xA2, 0xD9, 0xBC, 0xBE, 0xBD,
    0xF4, 0xBD, 0xBD, 0xBE, 0xD8, 0xBD, 0xA3, 0xA2, 0xD8, 0xBE, 0xBD, 0xBD,
    0xF4, 0xBD, 0xBD, 0xBE, 0xD8, 0xBD, 0xA3, 0xA2, 0xD9, 0xBD, 0xBE, 0xBD,
    0xF4, 0xBD, 0xBD, 0xBE, 0xD8, 0xBE, 0xA2, 0xA3, 0xD8, 0xBD, 0xBE, 0xBD,
    0xF4, 0xBD, 0xBE, 0xBE, 0xD8, 0xA2, 0xA2, 0xA2, 0xF4, 0xBE, 0xBD, 0xBE,
    0xD9, 0xA2, 0xA3, 0xA2, 0xF4, 0xBE, 0xBD, 0xBE,
};

#define ARRAY_SIZE(a) (sizeof(a)/sizeof(a[0]))

/* Linear delay model: delay = (A - prev) / B
 * Calibrate A,B from two known 2T points in the data */
static int delay_A = 420;  /* Default */
static int delay_B = 3;

static int calc_delay(int prev) {
    int d = (delay_A - prev) / delay_B;
    if (d < 20) d = 20;
    if (d > 130) d = 130;
    return d;
}

static int classify(int interval, int prev) {
    int delay = calc_delay(prev);
    int cells = (interval - delay + 16) / 32;
    if (cells < 2) cells = 2;
    if (cells > 4) cells = 5;
    return cells;
}

/* Calibrate A,B from raw data */
static void calibrate(const uint8_t *data, int count) {
    /* Find two reference points:
     * 1. 2T after the longest interval (minimum delay)
     * 2. 2T after the shortest interval (maximum delay)
     *
     * Strategy: find the smallest interval after a large prev,
     *           and the smallest interval after a small prev. */
    int min_after_high = 999, prev_of_min_high = 0;
    int min_after_low = 999, prev_of_min_low = 0;

    for (int i = 1; i < count; i++) {
        if (data[i] < 50) continue;  /* Skip startup */
        if (data[i-1] < 50) continue;

        /* "High prev" = top 25% of values */
        if (data[i-1] >= 200 && data[i] < min_after_high) {
            min_after_high = data[i];
            prev_of_min_high = data[i-1];
        }
        /* "Low prev" = bottom 25% of values */
        if (data[i-1] < 170 && data[i-1] >= 100 && data[i] < 170 && data[i] >= 100) {
            if (data[i] < min_after_low || min_after_low == 999) {
                min_after_low = data[i];
                prev_of_min_low = data[i-1];
            }
        }
    }

    if (min_after_high == 999 || min_after_low == 999) {
        printf("  Calibration: insufficient data, using defaults\n");
        return;
    }

    /* These minimum values are likely 2T (64 ticks nominal) */
    int delay_high = min_after_high - 64;  /* Delay after high prev */
    int delay_low = min_after_low - 64;    /* Delay after low prev */

    /* Solve linear: delay = (A - prev) / B
     * delay_high = (A - prev_high) / B
     * delay_low = (A - prev_low) / B
     *
     * delay_high * B = A - prev_high
     * delay_low * B = A - prev_low
     * (delay_high - delay_low) * B = prev_low - prev_high
     * B = (prev_low - prev_high) / (delay_high - delay_low)
     */
    if (delay_low == delay_high) {
        printf("  Calibration: delays equal, using defaults\n");
        return;
    }

    delay_B = (prev_of_min_low - prev_of_min_high) / (delay_high - delay_low);
    if (delay_B == 0) delay_B = 3;
    if (delay_B < 0) delay_B = -delay_B;
    delay_A = delay_high * delay_B + prev_of_min_high;

    printf("  Calibration: 2T after prev=%d → %d (delay=%d)\n",
           prev_of_min_high, min_after_high, delay_high);
    printf("               2T after prev=%d → %d (delay=%d)\n",
           prev_of_min_low, min_after_low, delay_low);
    printf("  Model: delay = (%d - prev) / %d\n", delay_A, delay_B);
}

static void decode_and_print(const uint8_t *data, int count) {
    printf("  Pulse codes: ");
    int prev = 200;
    int ncodes = 0;
    for (int i = 0; i < count; i++) {
        int v = data[i];
        if (v < 50) { prev = 200; continue; }
        int c = classify(v, prev);
        printf("%c", c <= 4 ? '0' + c : '?');
        ncodes++;
        if ((ncodes % 72) == 0) printf("\n               ");
        prev = v;
    }
    printf("\n\n");

    /* Decode bytes at both offsets */
    for (int off = 0; off <= 1; off++) {
        printf("  Bytes off=%d: ", off);
        uint32_t bits = 0;
        uint8_t avail = 0;
        uint8_t dbyte = 0, dbits = 0;
        int dcount = 0;
        prev = 200;

        for (int i = 0; i < count && dcount < 30; i++) {
            int v = data[i];
            if (v < 50) { prev = 200; continue; }
            int cells = classify(v, prev);
            if (cells > 4) { prev = v; continue; }

            bits = (bits << cells) | 1;
            avail += cells;
            if (avail > 30) avail = 30;

            while (avail >= 2 && dcount < 30) {
                avail -= 2;
                dbyte = (dbyte << 1) | ((bits >> (avail + off)) & 1);
                dbits++;
                if (dbits >= 8) {
                    printf("%02X ", dbyte);
                    dbyte = 0; dbits = 0; dcount++;
                }
            }
            prev = v;
        }
        printf("\n");
    }

    /* Search for patterns */
    printf("\n  Pattern search:\n");
    for (int off = 0; off <= 1; off++) {
        uint32_t bits = 0;
        uint8_t avail = 0;
        uint8_t dbyte = 0, dbits = 0;
        uint8_t window[8] = {0};
        int wpos = 0;
        prev = 200;
        int consecutive_zero = 0;

        for (int i = 0; i < count; i++) {
            int v = data[i];
            if (v < 50) { prev = 200; continue; }
            int cells = classify(v, prev);
            if (cells > 4) { prev = v; continue; }

            bits = (bits << cells) | 1;
            avail += cells;
            if (avail > 30) avail = 30;

            while (avail >= 2) {
                avail -= 2;
                dbyte = (dbyte << 1) | ((bits >> (avail + off)) & 1);
                dbits++;
                if (dbits >= 8) {
                    window[wpos % 8] = dbyte;
                    wpos++;
                    dbyte = 0; dbits = 0;

                    if (dbyte == 0x00) consecutive_zero++;
                    else consecutive_zero = 0;

                    if (window[(wpos-1)%8] == 0x00) {
                        /* Count consecutive zeros */
                        int zeros = 0;
                        for (int z = 1; z <= 8 && z <= wpos; z++) {
                            if (window[(wpos-z)%8] == 0x00) zeros++;
                            else break;
                        }
                        if (zeros == 4) {
                            printf("    off=%d byte=%d: PREAMBLE (4+ zeros)\n", off, wpos-4);
                        }
                    }
                    if (window[(wpos-1)%8] == 0xA1 && wpos >= 2 &&
                        window[(wpos-2)%8] == 0xA1) {
                        printf("    off=%d byte=%d: SYNC A1 A1\n", off, wpos-2);
                    }
                    if (window[(wpos-1)%8] == 0xFE) {
                        printf("    off=%d byte=%d: possible IDAM (0xFE)\n", off, wpos-1);
                    }
                    if (window[(wpos-1)%8] == 0x4E && wpos >= 2 &&
                        window[(wpos-2)%8] == 0x4E) {
                        printf("    off=%d byte=%d: GAP FILL (4E 4E)\n", off, wpos-2);
                    }
                }
            }
            prev = v;
        }
    }
}

int main(void) {
    printf("=== Linear delay model: delay = (A - prev) / B ===\n\n");

    printf("--- Trace 1: Clean data (preamble trace) ---\n");
    delay_A = 420; delay_B = 3;  /* Reset defaults */
    calibrate(trace_clean, ARRAY_SIZE(trace_clean));
    decode_and_print(trace_clean, ARRAY_SIZE(trace_clean));

    printf("\n--- Trace 2: Attempt 4 (tight clusters) ---\n");
    delay_A = 420; delay_B = 3;
    calibrate(trace_attempt4, ARRAY_SIZE(trace_attempt4));
    decode_and_print(trace_attempt4, ARRAY_SIZE(trace_attempt4));

    return 0;
}
