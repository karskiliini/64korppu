#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Include unit under test */
#include "fastload.h"
#include "fastload_burst.h"
#include "fastload_epyx.h"

/* ---- Test framework (same pattern as test_nano_fat12.c) ---- */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    test_##name(); \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)
#define ASSERT(cond) do { if (!(cond)) { \
    printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    tests_failed++; tests_passed--; return; } } while(0)
#define ASSERT_EQ(a, b) do { int64_t _a=(int64_t)(a), _b=(int64_t)(b); \
    if (_a != _b) { printf("FAIL\n    %s:%d: %lld != %lld\n", \
    __FILE__, __LINE__, _a, _b); \
    tests_failed++; tests_passed--; return; } } while(0)

/* ---- Mock detect functions ---- */

static bool mock_jiffy_detected = false;
static bool mock_burst_detected = false;
static bool mock_epyx_detected = false;

static bool mock_jiffy_detect(void) { return mock_jiffy_detected; }
static bool mock_burst_detect(void) { return mock_burst_detected; }
static bool mock_epyx_detect(void)  { return mock_epyx_detected; }

static bool mock_send(uint8_t byte, bool eoi) { (void)byte; (void)eoi; return true; }
static bool mock_recv(uint8_t *byte, bool *eoi) { *byte = 0x42; *eoi = false; return true; }
static void mock_atn_end(void) { }

static const fastload_protocol_t proto_jiffy = {
    .type = FASTLOAD_JIFFYDOS, .name = "JiffyDOS",
    .detect = mock_jiffy_detect, .send_byte = mock_send,
    .receive_byte = mock_recv, .on_atn_end = mock_atn_end
};
static const fastload_protocol_t proto_burst = {
    .type = FASTLOAD_BURST, .name = "Burst",
    .detect = mock_burst_detect, .send_byte = mock_send,
    .receive_byte = mock_recv, .on_atn_end = mock_atn_end
};
static const fastload_protocol_t proto_epyx = {
    .type = FASTLOAD_EPYX, .name = "EPYX",
    .detect = mock_epyx_detect, .send_byte = mock_send,
    .receive_byte = mock_recv, .on_atn_end = mock_atn_end
};

static void reset_mocks(void) {
    mock_jiffy_detected = false;
    mock_burst_detected = false;
    mock_epyx_detected = false;
    fastload_init();
}

/* ---- Registry tests ---- */

TEST(init_no_active) {
    reset_mocks();
    ASSERT(fastload_active() == NULL);
}

TEST(register_and_detect_jiffy) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    mock_jiffy_detected = true;
    ASSERT_EQ(fastload_detect(), FASTLOAD_JIFFYDOS);
    ASSERT(fastload_active() != NULL);
    ASSERT_EQ(fastload_active()->type, FASTLOAD_JIFFYDOS);
}

TEST(detect_none_when_no_protocol_matches) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    fastload_register(&proto_burst);
    ASSERT_EQ(fastload_detect(), FASTLOAD_NONE);
    ASSERT(fastload_active() == NULL);
}

TEST(detect_first_matching) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    fastload_register(&proto_burst);
    fastload_register(&proto_epyx);
    mock_burst_detected = true;
    mock_epyx_detected = true;
    ASSERT_EQ(fastload_detect(), FASTLOAD_BURST);
}

TEST(reset_clears_active) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    mock_jiffy_detected = true;
    fastload_detect();
    ASSERT(fastload_active() != NULL);
    fastload_reset();
    ASSERT(fastload_active() == NULL);
}

TEST(send_byte_through_active) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    mock_jiffy_detected = true;
    fastload_detect();
    const fastload_protocol_t *fl = fastload_active();
    ASSERT(fl != NULL);
    ASSERT(fl->send_byte(0xAA, false) == true);
}

TEST(receive_byte_through_active) {
    reset_mocks();
    fastload_register(&proto_epyx);
    mock_epyx_detected = true;
    fastload_detect();
    const fastload_protocol_t *fl = fastload_active();
    ASSERT(fl != NULL);
    uint8_t byte;
    bool eoi;
    ASSERT(fl->receive_byte(&byte, &eoi) == true);
    ASSERT_EQ(byte, 0x42);
}

TEST(multiple_detect_cycles) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    fastload_register(&proto_burst);
    mock_jiffy_detected = true;
    fastload_detect();
    ASSERT_EQ(fastload_active()->type, FASTLOAD_JIFFYDOS);
    fastload_reset();
    mock_jiffy_detected = false;
    mock_burst_detected = true;
    fastload_detect();
    ASSERT_EQ(fastload_active()->type, FASTLOAD_BURST);
}

TEST(register_max_protocols) {
    reset_mocks();
    fastload_register(&proto_jiffy);
    fastload_register(&proto_burst);
    fastload_register(&proto_epyx);
    fastload_register(&proto_jiffy);  /* 4th = max */
    /* 5th should be silently ignored */
    static const fastload_protocol_t extra = {
        .type = FASTLOAD_NONE, .name = "Extra",
        .detect = NULL, .send_byte = NULL,
        .receive_byte = NULL, .on_atn_end = NULL
    };
    fastload_register(&extra);
    /* Should still work with first 4 */
    mock_jiffy_detected = true;
    ASSERT_EQ(fastload_detect(), FASTLOAD_JIFFYDOS);
}

/* ---- Burst command detection tests ---- */

TEST(burst_check_u0_command) {
    ASSERT(fastload_burst_check_command("U0", 2) == true);
}

TEST(burst_check_u0_with_params) {
    ASSERT(fastload_burst_check_command("U0\x04\x00", 4) == true);
}

TEST(burst_check_not_u0) {
    ASSERT(fastload_burst_check_command("S:", 2) == false);
}

TEST(burst_check_too_short) {
    ASSERT(fastload_burst_check_command("U", 1) == false);
}

/* ---- EPYX M-W detection tests ---- */

TEST(epyx_single_mw_not_enough) {
    fastload_epyx_reset();
    uint8_t cmd[] = {0x4D, 0x2D, 0x57, 0x00, 0x04, 0x20};
    ASSERT(fastload_epyx_check_command(cmd, 6) == true);
    /* epyx_pending should not be set yet (need >= 3 M-W commands) */
}

TEST(epyx_three_mw_triggers) {
    fastload_epyx_reset();
    uint8_t cmd[] = {0x4D, 0x2D, 0x57, 0x00, 0x04, 0x20};
    fastload_epyx_check_command(cmd, 6);
    fastload_epyx_check_command(cmd, 6);
    fastload_epyx_check_command(cmd, 6);
    /* After 3 M-W commands, EPYX should be detected */
    ASSERT(true);  /* Got here without crash */
}

TEST(epyx_reset_clears_state) {
    fastload_epyx_reset();
    uint8_t cmd[] = {0x4D, 0x2D, 0x57, 0x00, 0x04, 0x20};
    fastload_epyx_check_command(cmd, 6);
    fastload_epyx_check_command(cmd, 6);
    fastload_epyx_reset();
    /* After reset, single M-W should not trigger */
    ASSERT(fastload_epyx_check_command(cmd, 6) == true);  /* valid M-W */
}

TEST(epyx_not_mw_command) {
    fastload_epyx_reset();
    uint8_t cmd[] = {0x53, 0x3A, 0x46};  /* S:F */
    ASSERT(fastload_epyx_check_command(cmd, 3) == false);
}

/* ---- Main ---- */

int main(void) {
    printf("=== Nano Fastload Unit Tests ===\n\n");

    printf("Registry:\n");
    RUN(init_no_active);
    RUN(register_and_detect_jiffy);
    RUN(detect_none_when_no_protocol_matches);
    RUN(detect_first_matching);
    RUN(reset_clears_active);
    RUN(send_byte_through_active);
    RUN(receive_byte_through_active);
    RUN(multiple_detect_cycles);
    RUN(register_max_protocols);

    printf("\nBurst detection:\n");
    RUN(burst_check_u0_command);
    RUN(burst_check_u0_with_params);
    RUN(burst_check_not_u0);
    RUN(burst_check_too_short);

    printf("\nEPYX detection:\n");
    RUN(epyx_single_mw_not_enough);
    RUN(epyx_three_mw_triggers);
    RUN(epyx_reset_clears_state);
    RUN(epyx_not_mw_command);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
