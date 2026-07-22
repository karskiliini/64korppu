/*
 * Comprehensive host tests for the REAL firmware MFM read path.
 *
 * Unlike test_mfm_sync.c (which mirrors logic), this compiles the
 * actual firmware sources (mfm_codec.c decode + calibration) against
 * a mock SRAM, generates complete IBM HD tracks with an INDEPENDENT
 * MFM encoder + CRC implementation (mfm_track_gen.h), and verifies
 * byte-exact sector decode under realistic imperfections:
 *
 *   - all 18 sectors, nominal timing
 *   - timing jitter (±2 ticks = ±125ns per flux transition)
 *   - motor speed error (±1.5% and ±6%, with recalibration)
 *   - random rotational phase (capture starts anywhere on the track)
 *   - captures truncated to 32KB (23LC256 case)
 *   - noise glitches in gaps
 *   - corrupted data-field CRC → FLOPPY_ERR_CRC (no silent corruption)
 *   - corrupted ID-field CRC → sector not accepted
 *   - CRC cross-check: firmware bitwise CRC vs independent table CRC
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "config.h"
#include "mfm_codec.h"
#include "sram.h"
#include "mock_sram.h"
#include "mfm_track_gen.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { printf("\n    ASSERT FAIL: %s (line %d)\n", #cond, __LINE__); return 0; } \
} while (0)

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-50s ", #fn); \
    fflush(stdout); \
    if (fn()) { tests_passed++; printf("PASS\n"); } else { printf("FAIL\n"); } \
} while (0)

/* ---- Shared fixtures ---- */

static tg_track_t track;
static uint8_t sector_data[TG_SECTORS][TG_SECTOR_SIZE];
static uint8_t decoded[TG_SECTOR_SIZE];

/*
 * Load a capture window into the mock SRAM: `count` intervals starting
 * at circular index `start`, at speed factor `speed` (1.0 = nominal
 * 300 RPM) with uniform jitter of ±`jitter` ticks per interval.
 */
static uint16_t load_capture(const tg_track_t *t, int start, int count,
                             double speed, int jitter) {
    if (count > (int)SRAM_RAW_CAPTURE_SIZE) count = SRAM_RAW_CAPTURE_SIZE;
    sram_begin_seq_write(SRAM_RAW_CAPTURE);
    for (int i = 0; i < count; i++) {
        int cells = t->cells[(start + i) % t->count];
        double ticks = cells * 16.0 * speed;
        int j = jitter ? (int)(tg_rand() % (2 * jitter + 1)) - jitter : 0;
        int v = (int)lround(ticks) + j;
        if (v < 1) v = 1;
        if (v > 255) v = 255;
        sram_seq_write_byte((uint8_t)v);
    }
    sram_end_seq();
    mfm_set_capture_count((uint16_t)count);
    return (uint16_t)count;
}

/* Approximate interval index where sector r's ID field begins */
static int sector_start_index(const tg_track_t *t, int r) {
    double frac = (80.0 + (r - 1) * 628.0) / (double)TG_TRACK_BYTES;
    int idx = (int)(frac * t->count) - 500;
    if (idx < 0) idx += t->count;
    return idx;
}

static void build_nominal(void) {
    tg_fill_pattern(sector_data);
    tg_build_track(&track, 0, 0, (const uint8_t (*)[TG_SECTOR_SIZE])sector_data, 0, 0);
}

/* ---- Tests ---- */

static int test_crc_cross_check(void) {
    /* Firmware bitwise CRC must agree with the independent table CRC */
    tg_srand(0xC0FFEE);
    for (int n = 0; n < 100; n++) {
        uint8_t buf[64];
        int len = 1 + (int)(tg_rand() % 64);
        for (int i = 0; i < len; i++) buf[i] = (uint8_t)tg_rand();
        ASSERT(mfm_crc16(buf, (uint16_t)len, 0xFFFF) ==
               tg_crc(buf, len, 0xFFFF));
    }
    return 1;
}

static int test_track_stream_sanity(void) {
    build_nominal();
    /* One HD revolution: ~70-85k transitions, all cells 2..4 */
    ASSERT(track.count > 65000 && track.count < TG_MAX_INTERVALS);
    for (int i = 1; i < track.count; i++)   /* [0] carries lead-in gap */
        ASSERT(track.cells[i] >= 2 && track.cells[i] <= 4);
    return 1;
}

static int test_decode_all_sectors_nominal(void) {
    build_nominal();
    mfm_set_default_thresholds();
    for (int r = 1; r <= TG_SECTORS; r++) {
        load_capture(&track, sector_start_index(&track, r), 50000, 1.0, 0);
        memset(decoded, 0, sizeof(decoded));
        int rc = mfm_decode_sector((uint8_t)r, decoded);
        ASSERT(rc == FLOPPY_OK);
        ASSERT(memcmp(decoded, sector_data[r - 1], TG_SECTOR_SIZE) == 0);
    }
    return 1;
}

static int test_decode_all_sectors_with_jitter(void) {
    build_nominal();
    tg_srand(0xBEEF01);
    mfm_set_default_thresholds();
    for (int r = 1; r <= TG_SECTORS; r++) {
        load_capture(&track, sector_start_index(&track, r), 50000, 1.0, 2);
        mfm_calibrate_capture();
        int rc = mfm_decode_sector((uint8_t)r, decoded);
        ASSERT(rc == FLOPPY_OK);
        ASSERT(memcmp(decoded, sector_data[r - 1], TG_SECTOR_SIZE) == 0);
    }
    return 1;
}

static int test_motor_speed_variation(void) {
    build_nominal();
    static const double speeds[] = { 0.985, 1.015, 0.94, 1.06 };
    for (unsigned s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
        tg_srand(0xAB0000 + s);
        mfm_set_default_thresholds();
        load_capture(&track, sector_start_index(&track, 1), 50000,
                     speeds[s], 1);
        mfm_calibrate_capture();
        ASSERT(mfm_decode_sector(1, decoded) == FLOPPY_OK);
        ASSERT(memcmp(decoded, sector_data[0], TG_SECTOR_SIZE) == 0);

        load_capture(&track, sector_start_index(&track, 18), 50000,
                     speeds[s], 1);
        mfm_calibrate_capture();
        ASSERT(mfm_decode_sector(18, decoded) == FLOPPY_OK);
        ASSERT(memcmp(decoded, sector_data[17], TG_SECTOR_SIZE) == 0);
    }
    return 1;
}

static int test_random_phase(void) {
    /* Capture can start at ANY rotational position (like real reads).
     * With a 50000-interval window (~63% of the track) most attempts
     * must succeed; misses must fail cleanly with ERR_NO_SECTOR. */
    build_nominal();
    tg_srand(0x12345);
    int ok = 0, miss = 0;
    for (int n = 0; n < 30; n++) {
        int r = 1 + (int)(tg_rand() % TG_SECTORS);
        int start = (int)(tg_rand() % (uint32_t)track.count);
        mfm_set_default_thresholds();
        load_capture(&track, start, 50000, 1.0, 1);
        mfm_calibrate_capture();
        int rc = mfm_decode_sector((uint8_t)r, decoded);
        if (rc == FLOPPY_OK) {
            ASSERT(memcmp(decoded, sector_data[r - 1], TG_SECTOR_SIZE) == 0);
            ok++;
        } else {
            ASSERT(rc == FLOPPY_ERR_NO_SECTOR);
            miss++;
        }
    }
    /* ~63% coverage per attempt → expect a clear majority of hits */
    ASSERT(ok >= 15);
    ASSERT(ok + miss == 30);
    return 1;
}

static int test_truncated_capture_23lc256(void) {
    /* 23LC256: capture clamped to 32768 intervals (~41% of track) */
    build_nominal();
    mock_sram_set_capacity(32768);
    mfm_set_default_thresholds();
    load_capture(&track, sector_start_index(&track, 2), 32768, 1.0, 0);
    mfm_calibrate_capture();
    int rc = mfm_decode_sector(2, decoded);
    mock_sram_set_capacity(65536);
    ASSERT(rc == FLOPPY_OK);
    ASSERT(memcmp(decoded, sector_data[1], TG_SECTOR_SIZE) == 0);
    return 1;
}

static int test_sector_not_in_window(void) {
    /* Short window far away from sector 1 → clean ERR_NO_SECTOR */
    build_nominal();
    mfm_set_default_thresholds();
    load_capture(&track, sector_start_index(&track, 10), 3000, 1.0, 0);
    ASSERT(mfm_decode_sector(1, decoded) == FLOPPY_ERR_NO_SECTOR);
    return 1;
}

static int test_noise_glitches_in_gaps(void) {
    /* Corrupt 200 intervals well before the target sector: the
     * decoder must resynchronize and still deliver sector 9. */
    build_nominal();
    tg_srand(0x600D);
    mfm_set_default_thresholds();
    int start = sector_start_index(&track, 9);
    load_capture(&track, (start + track.count - 15000) % track.count,
                 50000, 1.0, 0);
    uint8_t *mem = mock_sram_mem();
    for (int n = 0; n < 200; n++) {
        int idx = (int)(tg_rand() % 10000);   /* glitches in first part */
        mem[SRAM_RAW_CAPTURE + idx] = (uint8_t)(5 + tg_rand() % 11);
    }
    mfm_calibrate_capture();
    int rc = mfm_decode_sector(9, decoded);
    ASSERT(rc == FLOPPY_OK);
    ASSERT(memcmp(decoded, sector_data[8], TG_SECTOR_SIZE) == 0);
    return 1;
}

static int test_corrupt_data_crc_detected(void) {
    /* Sector 3 has a wrong data CRC on the disk: decoder must return
     * FLOPPY_ERR_CRC, NOT silently deliver the data. */
    tg_fill_pattern(sector_data);
    tg_build_track(&track, 0, 0,
                   (const uint8_t (*)[TG_SECTOR_SIZE])sector_data,
                   (1u << 2) /* sector 3 */, 0);
    mfm_set_default_thresholds();
    load_capture(&track, sector_start_index(&track, 3), 20000, 1.0, 0);
    ASSERT(mfm_decode_sector(3, decoded) == FLOPPY_ERR_CRC);

    /* Neighbouring sector 4 in the same window still decodes fine */
    ASSERT(mfm_decode_sector(4, decoded) == FLOPPY_OK);
    ASSERT(memcmp(decoded, sector_data[3], TG_SECTOR_SIZE) == 0);
    return 1;
}

static int test_corrupt_id_crc_rejected(void) {
    /* Sector 7's ID field CRC is wrong: that IDAM must not be
     * accepted, so the sector is not found in a single-pass window. */
    tg_fill_pattern(sector_data);
    tg_build_track(&track, 0, 0,
                   (const uint8_t (*)[TG_SECTOR_SIZE])sector_data,
                   0, (1u << 6) /* sector 7 */);
    mfm_set_default_thresholds();
    load_capture(&track, sector_start_index(&track, 7), 20000, 1.0, 0);
    ASSERT(mfm_decode_sector(7, decoded) == FLOPPY_ERR_NO_SECTOR);

    /* Sector 8 unaffected */
    ASSERT(mfm_decode_sector(8, decoded) == FLOPPY_OK);
    ASSERT(memcmp(decoded, sector_data[7], TG_SECTOR_SIZE) == 0);
    return 1;
}

static int test_calibration_tracks_speed(void) {
    /* At +6% speed the 4T cluster sits at ~68 ticks; after calibration
     * thresholds must follow the clusters and decode still works. */
    build_nominal();
    tg_srand(0x51EED);
    mfm_set_default_thresholds();
    load_capture(&track, sector_start_index(&track, 5), 50000, 1.06, 2);
    mfm_calibrate_capture();
    ASSERT(mfm_decode_sector(5, decoded) == FLOPPY_OK);
    ASSERT(memcmp(decoded, sector_data[4], TG_SECTOR_SIZE) == 0);
    return 1;
}

int main(void) {
    printf("=== MFM Decode Tests (real firmware code) ===\n\n");

    sram_init();
    mock_sram_set_capacity(65536);

    RUN_TEST(test_crc_cross_check);
    RUN_TEST(test_track_stream_sanity);
    RUN_TEST(test_decode_all_sectors_nominal);
    RUN_TEST(test_decode_all_sectors_with_jitter);
    RUN_TEST(test_motor_speed_variation);
    RUN_TEST(test_random_phase);
    RUN_TEST(test_truncated_capture_23lc256);
    RUN_TEST(test_sector_not_in_window);
    RUN_TEST(test_noise_glitches_in_gaps);
    RUN_TEST(test_corrupt_data_crc_detected);
    RUN_TEST(test_corrupt_id_crc_rejected);
    RUN_TEST(test_calibration_tracks_speed);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");
    return tests_passed == tests_run ? 0 : 1;
}
