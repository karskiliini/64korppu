/*
 * Unit tests for 74HC595 shift register bit mapping and floppy control.
 *
 * Verifies that firmware SR_BIT_* constants match the hardware:
 *   74HC595 DIP-16 pinout (from KiCad schematic):
 *     Pin 15 (QA, bit 0) → FLOPPY_SIDE1
 *     Pin  1 (QB, bit 1) → FLOPPY_DENSITY
 *     Pin  2 (QC, bit 2) → FLOPPY_MOTEA
 *     Pin  3 (QD, bit 3) → FLOPPY_DRVSEL
 *     Pin  4 (QE, bit 4) → FLOPPY_MOTOR
 *     Pin  5 (QF, bit 5) → FLOPPY_DIR
 *     Pin  6 (QG, bit 6) → FLOPPY_STEP
 *     Pin  7 (QH, bit 7) → FLOPPY_WGATE
 *
 * These tests would have caught the off-by-one bit mapping bug where
 * all signals were shifted by one position.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "shiftreg.h"

/* ---- Test framework (same as other test files) ---- */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(void); \
    static void run_test_##name(void) { \
        printf("  %-50s ", #name); \
        fflush(stdout); \
        test_##name(); \
        printf("PASS\n"); \
        tests_passed++; \
    } \
    static void test_##name(void)

#define RUN(name) do { \
    tests_run++; \
    run_test_##name(); \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    Assertion failed: %s\n    at %s:%d\n", \
               #cond, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a != _b) { \
        printf("FAIL\n    Expected %s == %s, got %lld != %lld\n    at %s:%d\n", \
               #a, #b, _a, _b, __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ---- Mock shift register state ---- */

static uint8_t mock_sr_state = SR_DEFAULT;
static uint8_t mock_sr_history[256];
static int mock_sr_count = 0;

void shiftreg_init(void) {
    mock_sr_state = SR_DEFAULT;
    mock_sr_count = 0;
}

void shiftreg_write(uint8_t value) {
    mock_sr_state = value;
    if (mock_sr_count < 256)
        mock_sr_history[mock_sr_count++] = value;
}

void shiftreg_set_bit(uint8_t bit, uint8_t value) {
    if (value)
        mock_sr_state |= (1 << bit);
    else
        mock_sr_state &= ~(1 << bit);
    shiftreg_write(mock_sr_state);
}

void shiftreg_assert_bit(uint8_t bit) {
    mock_sr_state &= ~(1 << bit);
    shiftreg_write(mock_sr_state);
}

void shiftreg_release_bit(uint8_t bit) {
    mock_sr_state |= (1 << bit);
    shiftreg_write(mock_sr_state);
}

uint8_t shiftreg_get(void) {
    return mock_sr_state;
}

/* ================================================================== */
/* SECTION 1: Hardware bit mapping verification                       */
/* These tests verify config.h SR_BIT_* matches the actual hardware.  */
/* ================================================================== */

/*
 * 74HC595 data sheet: SPI shifts MSB first.
 * QH gets bit 7 (first shifted in), QA gets bit 0 (last shifted in).
 * Hardware pinout (from KiCad PCB U3 pad→net):
 *   QA (pin 15) = FLOPPY_SIDE1    → must be bit 0
 *   QB (pin  1) = FLOPPY_DENSITY  → must be bit 1
 *   QC (pin  2) = FLOPPY_MOTEA   → must be bit 2
 *   QD (pin  3) = FLOPPY_DRVSEL  → must be bit 3
 *   QE (pin  4) = FLOPPY_MOTOR   → must be bit 4
 *   QF (pin  5) = FLOPPY_DIR     → must be bit 5
 *   QG (pin  6) = FLOPPY_STEP    → must be bit 6
 *   QH (pin  7) = FLOPPY_WGATE   → must be bit 7
 */

TEST(side1_is_bit_0_qa)
{
    /* QA (pin 15) = /SIDE1 → must be bit 0 */
    ASSERT_EQ(SR_BIT_SIDE1, 0);
}

TEST(density_is_bit_1_qb)
{
    /* QB (pin 1) = /DENSITY → must be bit 1 */
    ASSERT_EQ(SR_BIT_DENSITY, 1);
}

TEST(motea_is_bit_2_qc)
{
    /* QC (pin 2) = /MOTEA → must be bit 2 */
    ASSERT_EQ(SR_BIT_MOTEA, 2);
}

TEST(drvsel_is_bit_3_qd)
{
    /* QD (pin 3) = /DRVSEL → must be bit 3 */
    ASSERT_EQ(SR_BIT_DRVSEL, 3);
}

TEST(motor_is_bit_4_qe)
{
    /* QE (pin 4) = /MOTOR → must be bit 4 */
    ASSERT_EQ(SR_BIT_MOTOR, 4);
}

TEST(dir_is_bit_5_qf)
{
    /* QF (pin 5) = /DIR → must be bit 5 */
    ASSERT_EQ(SR_BIT_DIR, 5);
}

TEST(step_is_bit_6_qg)
{
    /* QG (pin 6) = /STEP → must be bit 6 */
    ASSERT_EQ(SR_BIT_STEP, 6);
}

TEST(wgate_is_bit_7_qh)
{
    /* QH (pin 7) = /WGATE → must be bit 7 */
    ASSERT_EQ(SR_BIT_WGATE, 7);
}

TEST(all_bits_unique)
{
    /* No two signals may share a bit position */
    uint8_t mask = 0;
    mask |= (1 << SR_BIT_SIDE1);
    mask |= (1 << SR_BIT_DENSITY);
    mask |= (1 << SR_BIT_MOTEA);
    mask |= (1 << SR_BIT_DRVSEL);
    mask |= (1 << SR_BIT_MOTOR);
    mask |= (1 << SR_BIT_DIR);
    mask |= (1 << SR_BIT_STEP);
    mask |= (1 << SR_BIT_WGATE);
    /* All 8 bits must be set (= 0xFF) if all positions are unique */
    ASSERT_EQ(mask, 0xFF);
}

TEST(default_all_deasserted)
{
    /* SR_DEFAULT = 0xFF means all active-low signals deasserted (HIGH) */
    ASSERT_EQ(SR_DEFAULT, 0xFF);
}

/* ================================================================== */
/* SECTION 2: Shift register logic                                     */
/* Tests assert/release/write operations on mock shift register.       */
/* ================================================================== */

TEST(init_sets_default)
{
    shiftreg_init();
    ASSERT_EQ(shiftreg_get(), SR_DEFAULT);
}

TEST(assert_clears_bit)
{
    shiftreg_init();
    shiftreg_assert_bit(SR_BIT_MOTOR);
    /* Active-low: asserting clears the bit */
    ASSERT_EQ(shiftreg_get() & (1 << SR_BIT_MOTOR), 0);
    /* Other bits unchanged */
    ASSERT(shiftreg_get() & (1 << SR_BIT_STEP));
}

TEST(release_sets_bit)
{
    shiftreg_init();
    shiftreg_assert_bit(SR_BIT_MOTOR);
    shiftreg_release_bit(SR_BIT_MOTOR);
    ASSERT(shiftreg_get() & (1 << SR_BIT_MOTOR));
    ASSERT_EQ(shiftreg_get(), SR_DEFAULT);
}

TEST(motor_only_affects_motor_bit)
{
    /* Asserting MOTOR must only affect bit 4, nothing else */
    shiftreg_init();
    uint8_t before = shiftreg_get();
    shiftreg_assert_bit(SR_BIT_MOTOR);
    uint8_t after = shiftreg_get();
    uint8_t changed = before ^ after;
    ASSERT_EQ(changed, (1 << SR_BIT_MOTOR));
}

TEST(step_only_affects_step_bit)
{
    /* Asserting STEP must only affect bit 6, nothing else */
    shiftreg_init();
    uint8_t before = shiftreg_get();
    shiftreg_assert_bit(SR_BIT_STEP);
    uint8_t after = shiftreg_get();
    uint8_t changed = before ^ after;
    ASSERT_EQ(changed, (1 << SR_BIT_STEP));
}

/* ================================================================== */
/* SECTION 3: Byte pattern verification                                */
/* Verify that specific operations produce the correct SPI bytes.      */
/* ================================================================== */

TEST(floppy_init_pattern)
{
    /*
     * floppy_init() should assert /DENSITY and /DRVSEL:
     *   bit 1 (DENSITY) = 0, bit 3 (DRVSEL) = 0
     *   Result: 0xFF & ~(1<<1) & ~(1<<3) = 0xFF & 0xFD & 0xF7 = 0xF5
     */
    shiftreg_init();
    mock_sr_count = 0;

    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);

    ASSERT_EQ(mock_sr_state, 0xF5);
    /* Verify specific pins: */
    ASSERT_EQ(mock_sr_state & (1 << SR_BIT_DENSITY), 0);  /* asserted */
    ASSERT_EQ(mock_sr_state & (1 << SR_BIT_DRVSEL), 0);   /* asserted */
    ASSERT(mock_sr_state & (1 << SR_BIT_MOTOR));           /* deasserted */
    ASSERT(mock_sr_state & (1 << SR_BIT_SIDE1));           /* deasserted */
}

TEST(motor_on_pattern)
{
    /*
     * After floppy_init + motor_on:
     *   DENSITY=asserted(0), DRVSEL=asserted(0),
     *   MOTEA=asserted(0), MOTOR=asserted(0)
     *   bits 1,2,3,4 = 0 → 0xFF & ~0x1E = 0xE1
     */
    shiftreg_init();
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);

    shiftreg_assert_bit(SR_BIT_MOTEA);
    shiftreg_assert_bit(SR_BIT_MOTOR);

    ASSERT_EQ(mock_sr_state, 0xE1);
}

TEST(step_outward_pattern)
{
    /*
     * Step outward (toward track 0):
     *   DIR released (HIGH, bit 5 = 1) → outward direction
     *   STEP asserted (LOW, bit 6 = 0)
     *   With DENSITY+DRVSEL+MOTEA+MOTOR already asserted:
     *   0xE1 & ~(1<<6) = 0xE1 & 0xBF = 0xA1
     */
    shiftreg_init();
    /* Setup: init + motor on */
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);
    shiftreg_assert_bit(SR_BIT_MOTEA);
    shiftreg_assert_bit(SR_BIT_MOTOR);

    /* Step outward: DIR released (=outward), then STEP asserted */
    shiftreg_release_bit(SR_BIT_DIR);
    shiftreg_assert_bit(SR_BIT_STEP);

    ASSERT_EQ(mock_sr_state, 0xA1);
    /* DIR bit should be HIGH (outward) */
    ASSERT(mock_sr_state & (1 << SR_BIT_DIR));
    /* STEP bit should be LOW (asserted) */
    ASSERT_EQ(mock_sr_state & (1 << SR_BIT_STEP), 0);
}

TEST(step_inward_pattern)
{
    /*
     * Step inward (toward center):
     *   DIR asserted (LOW, bit 5 = 0) → inward direction
     *   STEP asserted (LOW, bit 6 = 0)
     *   With DENSITY+DRVSEL+MOTEA+MOTOR already asserted:
     *   0xE1 & ~(1<<5) & ~(1<<6) = 0xE1 & 0x9F = 0x81
     */
    shiftreg_init();
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);
    shiftreg_assert_bit(SR_BIT_MOTEA);
    shiftreg_assert_bit(SR_BIT_MOTOR);

    shiftreg_assert_bit(SR_BIT_DIR);
    shiftreg_assert_bit(SR_BIT_STEP);

    ASSERT_EQ(mock_sr_state, 0x81);
    /* DIR bit should be LOW (inward) */
    ASSERT_EQ(mock_sr_state & (1 << SR_BIT_DIR), 0);
}

TEST(side1_select_pattern)
{
    /*
     * Select side 1: assert /SIDE1 (bit 0 = 0)
     * With motor on (0xE1): 0xE1 & ~(1<<0) = 0xE0
     */
    shiftreg_init();
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);
    shiftreg_assert_bit(SR_BIT_MOTEA);
    shiftreg_assert_bit(SR_BIT_MOTOR);

    shiftreg_assert_bit(SR_BIT_SIDE1);

    ASSERT_EQ(mock_sr_state, 0xE0);
    ASSERT_EQ(mock_sr_state & (1 << SR_BIT_SIDE1), 0);
}

TEST(wgate_pattern)
{
    /*
     * Assert write gate: bit 7 = 0
     * With motor on (0xE1): 0xE1 & ~(1<<7) = 0x61
     */
    shiftreg_init();
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);
    shiftreg_assert_bit(SR_BIT_MOTEA);
    shiftreg_assert_bit(SR_BIT_MOTOR);

    shiftreg_assert_bit(SR_BIT_WGATE);

    ASSERT_EQ(mock_sr_state, 0x61);
}

/* ================================================================== */
/* SECTION 4: Cross-validation with SimAVR simulation output           */
/* Verify patterns match what SimAVR observed during boot.             */
/* ================================================================== */

TEST(simavr_boot_sequence_matches)
{
    /*
     * SimAVR boot log showed:
     *   Latch #2: 0xFF → 0xF5  (DENSITY=ON DRVSEL=ON)
     *   Latch #3: 0xF5 → 0xF1  (MOTEA=ON)
     *   Latch #4: 0xF1 → 0xE1  (MOTOR=ON)
     *   Latch #6: 0xE1 → 0xA1  (STEP=ON)
     *   Latch #7: 0xA1 → 0xE1  (STEP=off)
     *
     * Reproduce this sequence with mock shift register:
     */
    shiftreg_init();
    mock_sr_count = 0;

    /* Step 1: floppy_init → assert DENSITY + DRVSEL */
    uint8_t sr = SR_DEFAULT;
    sr &= ~(1 << SR_BIT_DENSITY);
    sr &= ~(1 << SR_BIT_DRVSEL);
    shiftreg_write(sr);
    ASSERT_EQ(mock_sr_history[0], 0xF5);

    /* Step 2: motor_on → assert MOTEA */
    shiftreg_assert_bit(SR_BIT_MOTEA);
    ASSERT_EQ(mock_sr_history[1], 0xF1);

    /* Step 3: motor_on → assert MOTOR */
    shiftreg_assert_bit(SR_BIT_MOTOR);
    ASSERT_EQ(mock_sr_history[2], 0xE1);

    /* Step 4: step outward → assert STEP */
    shiftreg_assert_bit(SR_BIT_STEP);
    ASSERT_EQ(mock_sr_history[3], 0xA1);

    /* Step 5: step done → release STEP */
    shiftreg_release_bit(SR_BIT_STEP);
    ASSERT_EQ(mock_sr_history[4], 0xE1);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(void)
{
    printf("=== Nano 74HC595 Shift Register Tests ===\n");

    printf("\nHardware bit mapping (KiCad PCB → config.h):\n");
    RUN(side1_is_bit_0_qa);
    RUN(density_is_bit_1_qb);
    RUN(motea_is_bit_2_qc);
    RUN(drvsel_is_bit_3_qd);
    RUN(motor_is_bit_4_qe);
    RUN(dir_is_bit_5_qf);
    RUN(step_is_bit_6_qg);
    RUN(wgate_is_bit_7_qh);
    RUN(all_bits_unique);
    RUN(default_all_deasserted);

    printf("\nShift register logic:\n");
    RUN(init_sets_default);
    RUN(assert_clears_bit);
    RUN(release_sets_bit);
    RUN(motor_only_affects_motor_bit);
    RUN(step_only_affects_step_bit);

    printf("\nByte patterns (floppy operations → SPI bytes):\n");
    RUN(floppy_init_pattern);
    RUN(motor_on_pattern);
    RUN(step_outward_pattern);
    RUN(step_inward_pattern);
    RUN(side1_select_pattern);
    RUN(wgate_pattern);

    printf("\nSimAVR cross-validation:\n");
    RUN(simavr_boot_sequence_matches);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return tests_failed ? 1 : 0;
}
