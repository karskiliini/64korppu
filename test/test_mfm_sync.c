/*
 * Regression test for MFM sync detection (mfm_codec.c DEC_SCAN_SYNC).
 *
 * mfm_codec.c is AVR-only (#ifdef __AVR__), so this test mirrors the
 * sync-window logic byte-for-byte instead of linking it. Keep the
 * mirrored block in sync with firmware/E-IEC-Nano-SRAM/src/mfm_codec.c.
 *
 * The invariant under test: consecutive 0x4489 sync matches are 16 raw
 * bits apart, so the match window does NOT match on the intervals in
 * between — and that must NOT reset the sync counter. A firmware
 * version that reset sync_count on every non-matching window could
 * never see 3 consecutive syncs, and mfm_decode_sector() failed on
 * every sector even with a perfect signal.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define MFM_RAW_SYNC 0x4489

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond) do { \
    if (!(cond)) { printf("    ASSERT FAIL: %s (line %d)\n", #cond, __LINE__); return false; } \
} while (0)

#define RUN_TEST(fn) do { \
    tests_run++; \
    printf("  %-50s ", #fn); \
    if (fn()) { tests_passed++; printf("PASS\n"); } else { printf("FAIL\n"); } \
} while (0)

/*
 * Build the interval stream for: 24x 2T preamble (0x00 bytes = raw
 * ...101010...) followed by 3x 0xA1 sync (raw 0x4489, transitions at
 * bit positions 1,5,8,12,15 → intervals 4,3,4,3, gap-in 3/2).
 */
static int build_sync_stream(uint8_t *iv) {
    int n = 0;
    for (int i = 0; i < 24; i++) iv[n++] = 2;
    int first = 1;
    for (int s = 0; s < 3; s++) {
        iv[n++] = first ? 3 : 2;
        first = 0;
        iv[n++] = 4; iv[n++] = 3; iv[n++] = 4; iv[n++] = 3;
    }
    return n;
}

/* Mirror of the FIXED firmware logic: no reset on non-matching windows */
static int count_syncs_fixed(const uint8_t *iv, int n) {
    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;

    for (int pos = 0; pos < n; pos++) {
        uint8_t cells = iv[pos];
        raw_bits = (raw_bits << cells) | 1;
        raw_avail += cells;
        if (raw_avail > 30) raw_avail = 30;

        if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
            sync_count++;
            if (sync_count >= 3) return sync_count;
        }
    }
    return sync_count;
}

/* Mirror of the OLD (buggy) logic: reset whenever the window mismatches */
static int count_syncs_with_reset(const uint8_t *iv, int n) {
    uint32_t raw_bits = 0;
    uint8_t raw_avail = 0;
    uint8_t sync_count = 0;
    int max_seen = 0;

    for (int pos = 0; pos < n; pos++) {
        uint8_t cells = iv[pos];
        raw_bits = (raw_bits << cells) | 1;
        raw_avail += cells;
        if (raw_avail > 30) raw_avail = 30;

        if ((raw_bits & 0xFFFF) == MFM_RAW_SYNC) {
            sync_count++;
            if (sync_count > max_seen) max_seen = sync_count;
            if (sync_count >= 3) return sync_count;
        } else if (sync_count > 0) {
            if (sync_count < 3) sync_count = 0;
        }
    }
    return max_seen;
}

static bool test_fixed_logic_reaches_three_syncs(void) {
    uint8_t iv[64];
    int n = build_sync_stream(iv);
    ASSERT(count_syncs_fixed(iv, n) >= 3);
    return true;
}

static bool test_old_reset_logic_never_reaches_three(void) {
    /* Documents the original bug: with the mid-window reset the
     * counter can never exceed 1 even on perfect data. */
    uint8_t iv[64];
    int n = build_sync_stream(iv);
    ASSERT(count_syncs_with_reset(iv, n) == 1);
    return true;
}

static bool test_sync_pattern_is_unique_in_gap_bytes(void) {
    /* Gap fill 0x4E (legal clocked MFM: 3T,3T,3T,2T,2T,3T repeating)
     * must never produce a 0x4489 window. */
    static const uint8_t gap[6] = {3, 3, 3, 2, 2, 3};
    uint8_t iv[120];
    int n = 0;
    for (int b = 0; b < 20; b++)
        for (int i = 0; i < 6; i++) iv[n++] = gap[i];
    ASSERT(count_syncs_fixed(iv, n) == 0);
    return true;
}

int main(void) {
    printf("=== MFM Sync Detection Tests ===\n\n");

    RUN_TEST(test_fixed_logic_reaches_three_syncs);
    RUN_TEST(test_old_reset_logic_never_reaches_three);
    RUN_TEST(test_sync_pattern_is_unique_in_gap_bytes);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");
    return tests_passed == tests_run ? 0 : 1;
}
