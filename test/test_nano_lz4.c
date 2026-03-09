#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include units under test */
#include "lz4_compress.h"
#include "compress_proto.h"

/* ---- Test framework (same pattern as test_nano_fastload.c) ---- */

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

/* ---- Reference LZ4 block decompressor for roundtrip verification ---- */

/*
 * Decode a standard LZ4 block.
 * Returns decompressed size, or -1 on error.
 */
static int lz4_decompress_block(const uint8_t *src, int src_len,
                                uint8_t *dst, int dst_cap)
{
    const uint8_t *ip = src;
    const uint8_t *iend = src + src_len;
    uint8_t *op = dst;
    uint8_t *oend = dst + dst_cap;

    while (ip < iend) {
        /* Read token */
        uint8_t token = *ip++;
        int lit_len = token >> 4;
        int match_len = token & 0x0F;

        /* Literal length extension */
        if (lit_len == 15) {
            int s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                lit_len += s;
            } while (s == 255);
        }

        /* Copy literals */
        if (op + lit_len > oend) return -1;
        if (ip + lit_len > iend) return -1;
        memcpy(op, ip, lit_len);
        ip += lit_len;
        op += lit_len;

        /* Check if this was the last sequence (no offset follows) */
        if (ip >= iend)
            break;

        /* Read offset (little-endian) */
        if (ip + 2 > iend) return -1;
        int offset = ip[0] | (ip[1] << 8);
        ip += 2;
        if (offset == 0) return -1;

        /* Match length extension */
        match_len += 4;  /* MIN_MATCH */
        if ((token & 0x0F) == 15) {
            int s;
            do {
                if (ip >= iend) return -1;
                s = *ip++;
                match_len += s;
            } while (s == 255);
        }

        /* Copy match */
        if (op - offset < dst) return -1;
        if (op + match_len > oend) return -1;
        const uint8_t *ref = op - offset;
        for (int i = 0; i < match_len; i++)
            op[i] = ref[i];  /* byte-by-byte for overlapping */
        op += match_len;
    }

    return (int)(op - dst);
}

/* ---- Helper: roundtrip verify ---- */

static int roundtrip_ok(const uint8_t *data, int len)
{
    uint8_t compressed[2048];
    uint8_t decompressed[2048];

    int clen = lz4_compress_block(data, len, compressed, sizeof(compressed));
    if (clen < 0) return 0;
    if (clen == 0 && len == 0) return 1;

    int dlen = lz4_decompress_block(compressed, clen,
                                     decompressed, sizeof(decompressed));
    if (dlen != len) return 0;
    return memcmp(data, decompressed, len) == 0;
}

/* ---- Test cases ---- */

TEST(compress_empty_input) {
    uint8_t dst[64];
    int ret = lz4_compress_block(NULL, 0, dst, sizeof(dst));
    ASSERT_EQ(ret, 0);
}

TEST(compress_single_byte) {
    uint8_t src[] = {0x42};
    uint8_t dst[64];
    int ret = lz4_compress_block(src, 1, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(roundtrip_ok(src, 1));
}

TEST(compress_short_literal_only) {
    /* 8 bytes of varied data - too short for matches */
    uint8_t src[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t dst[64];
    int ret = lz4_compress_block(src, 8, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(roundtrip_ok(src, 8));
}

TEST(compress_repeated_data) {
    /* 256 bytes of the same value - must compress smaller */
    uint8_t src[256];
    memset(src, 0xAA, 256);
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 256, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(ret < 256);  /* Must actually compress */
    ASSERT(roundtrip_ok(src, 256));
}

TEST(compress_repeated_pattern) {
    /* 4-byte pattern repeating 64 times = 256 bytes */
    uint8_t src[256];
    for (int i = 0; i < 256; i++)
        src[i] = (uint8_t)(i % 4 + 1);  /* 1,2,3,4,1,2,3,4,... */
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 256, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(ret < 256);  /* Should compress well */
    ASSERT(roundtrip_ok(src, 256));
}

TEST(compress_incompressible_data) {
    /* Pseudo-random data: hard to compress */
    uint8_t src[128];
    uint32_t state = 0xDEADBEEF;
    for (int i = 0; i < 128; i++) {
        state = state * 1103515245 + 12345;
        src[i] = (uint8_t)(state >> 16);
    }
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 128, dst, sizeof(dst));
    ASSERT(ret > 0);  /* Should still produce valid output */
    ASSERT(roundtrip_ok(src, 128));
}

TEST(compress_output_buffer_too_small) {
    /* 128 bytes of random-ish data, but only 2 bytes of output space */
    uint8_t src[128];
    for (int i = 0; i < 128; i++)
        src[i] = (uint8_t)(i * 73 + 17);
    uint8_t dst[2];
    int ret = lz4_compress_block(src, 128, dst, sizeof(dst));
    ASSERT_EQ(ret, -1);
}

TEST(compress_typical_c64_basic) {
    /* Simulate C64 BASIC area: lots of zeros + some BASIC tokens */
    uint8_t src[256];
    memset(src, 0x00, 256);
    /* Sprinkle some BASIC tokens */
    src[0] = 0x01;   /* link low */
    src[1] = 0x08;   /* link high */
    src[2] = 0x0A;   /* line number low */
    src[3] = 0x00;   /* line number high */
    src[4] = 0x99;   /* PRINT token */
    src[5] = 0x22;   /* " */
    src[6] = 0x48;   /* H */
    src[7] = 0x45;   /* E */
    src[8] = 0x4C;   /* L */
    src[9] = 0x4C;   /* L */
    src[10] = 0x4F;  /* O */
    src[11] = 0x22;  /* " */
    src[12] = 0x00;  /* end of line */
    /* Rest is zeros - highly compressible */
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 256, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(ret < 256);  /* Should compress well (lots of zeros) */
    ASSERT(roundtrip_ok(src, 256));
}

TEST(compress_max_block_size) {
    /* 256 bytes - max expected block for this use case */
    uint8_t src[256];
    /* Mix of compressible and non-compressible sections */
    memset(src, 0x00, 128);
    for (int i = 128; i < 256; i++)
        src[i] = (uint8_t)(i);
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 256, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(roundtrip_ok(src, 256));
}

TEST(compress_long_match) {
    /* 32-byte pattern followed by an exact repeat -> triggers match extension
     * beyond 15 (the inline limit), testing the extension byte path */
    uint8_t src[256];
    /* First 32 bytes: identifiable pattern */
    for (int i = 0; i < 32; i++)
        src[i] = (uint8_t)(i + 0x40);
    /* Repeat the same 32 bytes to create a long match */
    for (int i = 32; i < 64; i++)
        src[i] = (uint8_t)((i - 32) + 0x40);
    /* Third repeat */
    for (int i = 64; i < 96; i++)
        src[i] = (uint8_t)((i - 64) + 0x40);
    /* Fill rest with zeros */
    memset(src + 96, 0x00, 160);
    uint8_t dst[512];
    int ret = lz4_compress_block(src, 256, dst, sizeof(dst));
    ASSERT(ret > 0);
    ASSERT(ret < 256);
    ASSERT(roundtrip_ok(src, 256));
}

/* ---- XZ command channel tests ---- */

TEST(xz_default_disabled) {
    compress_proto_init();
    ASSERT(!compress_proto_enabled());
}

TEST(xz_enable) {
    compress_proto_init();
    bool ret = compress_proto_handle_command("XZ:1", 4);
    ASSERT(ret);
    ASSERT(compress_proto_enabled());
}

TEST(xz_disable) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    ASSERT(compress_proto_enabled());
    bool ret = compress_proto_handle_command("XZ:0", 4);
    ASSERT(ret);
    ASSERT(!compress_proto_enabled());
}

TEST(xz_status_query_disabled) {
    compress_proto_init();
    compress_proto_handle_command("XZ:S", 4);
    char buf[16];
    int n = compress_proto_get_status(buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ(strcmp(buf, "XZ:0"), 0);
}

TEST(xz_status_query_enabled) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    compress_proto_handle_command("XZ:S", 4);
    char buf[16];
    int n = compress_proto_get_status(buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT_EQ(strcmp(buf, "XZ:1"), 0);
}

TEST(xz_not_xz_command) {
    compress_proto_init();
    bool ret = compress_proto_handle_command("S:FILE", 6);
    ASSERT(!ret);
}

TEST(xz_invalid_subcommand) {
    compress_proto_init();
    bool ret = compress_proto_handle_command("XZ:X", 4);
    ASSERT(!ret);
}

TEST(xz_case_insensitive) {
    compress_proto_init();
    bool ret = compress_proto_handle_command("xz:1", 4);
    ASSERT(ret);
    ASSERT(compress_proto_enabled());
}

TEST(xz_reset_disables) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    ASSERT(compress_proto_enabled());
    compress_proto_init();
    ASSERT(!compress_proto_enabled());
}

TEST(xz_get_status_without_query) {
    /* get_status should return -1 if XZ:S was not sent */
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    char buf[16];
    int n = compress_proto_get_status(buf, sizeof(buf));
    ASSERT_EQ(n, -1);
}

/* ---- Block framing tests ---- */

TEST(frame_block_header) {
    /* Compress 8 bytes, verify 4-byte header and roundtrip */
    uint8_t raw[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
    uint8_t out[128];
    int frame_len = compress_proto_frame_block(raw, 8, out, sizeof(out));
    ASSERT(frame_len > 4);  /* header + at least 1 byte payload */

    /* Parse header: comp_size LE, raw_size LE */
    uint16_t comp_size = (uint16_t)(out[0] | (out[1] << 8));
    uint16_t raw_size = (uint16_t)(out[2] | (out[3] << 8));
    ASSERT_EQ(raw_size, 8);
    ASSERT_EQ(frame_len, 4 + comp_size);

    /* Decompress payload and verify roundtrip */
    uint8_t decomp[64];
    int dlen = lz4_decompress_block(out + 4, comp_size, decomp, sizeof(decomp));
    ASSERT_EQ(dlen, 8);
    ASSERT_EQ(memcmp(raw, decomp, 8), 0);
}

TEST(frame_eof_marker) {
    uint8_t out[8];
    int ret = compress_proto_frame_eof(out, sizeof(out));
    ASSERT_EQ(ret, 2);
    ASSERT_EQ(out[0], 0x00);
    ASSERT_EQ(out[1], 0x00);
}

TEST(frame_256_byte_block) {
    /* 256 zeros: highly compressible */
    uint8_t raw[256];
    memset(raw, 0x00, 256);
    uint8_t out[512];
    int frame_len = compress_proto_frame_block(raw, 256, out, sizeof(out));
    ASSERT(frame_len > 4);

    /* Parse header */
    uint16_t comp_size = (uint16_t)(out[0] | (out[1] << 8));
    uint16_t raw_size = (uint16_t)(out[2] | (out[3] << 8));
    ASSERT_EQ(raw_size, 256);
    ASSERT(comp_size < 256);  /* Must actually compress */
    ASSERT_EQ(frame_len, 4 + comp_size);
}

/* ---- Main ---- */

int main(void) {
    printf("=== Nano LZ4 Compress Unit Tests ===\n\n");

    printf("Basic:\n");
    RUN(compress_empty_input);
    RUN(compress_single_byte);
    RUN(compress_short_literal_only);

    printf("\nCompression:\n");
    RUN(compress_repeated_data);
    RUN(compress_repeated_pattern);
    RUN(compress_incompressible_data);

    printf("\nEdge cases:\n");
    RUN(compress_output_buffer_too_small);
    RUN(compress_typical_c64_basic);
    RUN(compress_max_block_size);
    RUN(compress_long_match);

    printf("\nXZ command channel:\n");
    RUN(xz_default_disabled);
    RUN(xz_enable);
    RUN(xz_disable);
    RUN(xz_status_query_disabled);
    RUN(xz_status_query_enabled);
    RUN(xz_not_xz_command);
    RUN(xz_invalid_subcommand);
    RUN(xz_case_insensitive);
    RUN(xz_reset_disables);
    RUN(xz_get_status_without_query);

    printf("\nBlock framing:\n");
    RUN(frame_block_header);
    RUN(frame_eof_marker);
    RUN(frame_256_byte_block);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
