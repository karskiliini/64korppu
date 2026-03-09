# LZ4 Compression Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add on-the-fly LZ4 compression to the Nano firmware so that file data and directory listings are compressed before IEC transfer when activated via XZ command channel.

**Architecture:** Three new modules: `lz4_compress` (block compressor), `compress_proto` (block framing + XZ state), and changes to `cbm_dos.c` (command parsing + talk loop integration). Compression is opt-in via `XZ:1` command on SA 15. Block protocol sends 4-byte header (compressed size + raw size) followed by LZ4 payload, terminated by 0x0000 EOF marker.

**Tech Stack:** C (AVR-compatible), host tests with custom TAP framework, existing Makefile build system.

**Reference:** `docs/plans/2026-03-09-lz4-compression-design.md` and `docs/E-IEC-Nano-SRAM/lz4-protokolla.md`

---

### Task 1: LZ4 block compressor — tests

**Files:**
- Create: `test/test_nano_lz4.c`

Write all LZ4 compression tests first. The compressor takes a raw buffer and produces LZ4-block-format output.

**Step 1: Write test file with framework and all tests**

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "lz4_compress.h"

/* ---- Test framework ---- */

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

/* ---- LZ4 reference decompressor (for roundtrip tests) ---- */

/* Minimal LZ4 block decompressor for verification */
static int lz4_decompress_block(const uint8_t *src, int src_len,
                                 uint8_t *dst, int dst_cap) {
    const uint8_t *sp = src;
    const uint8_t *se = src + src_len;
    uint8_t *dp = dst;
    uint8_t *de = dst + dst_cap;

    while (sp < se) {
        uint8_t token = *sp++;
        /* Literal length */
        int lit_len = (token >> 4) & 0x0F;
        if (lit_len == 15) {
            uint8_t extra;
            do {
                if (sp >= se) return -1;
                extra = *sp++;
                lit_len += extra;
            } while (extra == 255);
        }
        /* Copy literals */
        if (sp + lit_len > se) return -1;
        if (dp + lit_len > de) return -1;
        memcpy(dp, sp, lit_len);
        sp += lit_len;
        dp += lit_len;

        if (sp >= se) break;  /* Last sequence has no match */

        /* Match offset */
        if (sp + 2 > se) return -1;
        int offset = sp[0] | (sp[1] << 8);
        sp += 2;
        if (offset == 0) return -1;
        if (dp - dst < offset) return -1;

        /* Match length */
        int match_len = (token & 0x0F) + 4;
        if (match_len == 19) {
            uint8_t extra;
            do {
                if (sp >= se) return -1;
                extra = *sp++;
                match_len += extra;
            } while (extra == 255);
        }
        /* Copy match (may overlap) */
        if (dp + match_len > de) return -1;
        const uint8_t *mp = dp - offset;
        for (int i = 0; i < match_len; i++) {
            dp[i] = mp[i];
        }
        dp += match_len;
    }
    return (int)(dp - dst);
}

/* ---- Tests ---- */

TEST(compress_empty_input) {
    uint8_t out[16];
    int result = lz4_compress_block(NULL, 0, out, sizeof(out));
    ASSERT_EQ(result, 0);
}

TEST(compress_single_byte) {
    uint8_t in[] = {0x42};
    uint8_t out[16];
    int result = lz4_compress_block(in, 1, out, sizeof(out));
    ASSERT(result > 0);
    /* Decompress and verify roundtrip */
    uint8_t dec[16];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 1);
    ASSERT_EQ(dec[0], 0x42);
}

TEST(compress_short_literal_only) {
    uint8_t in[] = "ABCDEFGH";
    uint8_t out[32];
    int result = lz4_compress_block(in, 8, out, sizeof(out));
    ASSERT(result > 0);
    uint8_t dec[32];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 8);
    ASSERT(memcmp(dec, in, 8) == 0);
}

TEST(compress_repeated_data) {
    /* Highly compressible: 256 bytes of same value */
    uint8_t in[256];
    memset(in, 0xAA, 256);
    uint8_t out[300];
    int result = lz4_compress_block(in, 256, out, sizeof(out));
    ASSERT(result > 0);
    ASSERT(result < 256);  /* Must be smaller */
    uint8_t dec[256];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 256);
    ASSERT(memcmp(dec, in, 256) == 0);
}

TEST(compress_repeated_pattern) {
    /* Repeating 4-byte pattern: compresses well */
    uint8_t in[256];
    for (int i = 0; i < 256; i++) in[i] = "ABCD"[i % 4];
    uint8_t out[300];
    int result = lz4_compress_block(in, 256, out, sizeof(out));
    ASSERT(result > 0);
    ASSERT(result < 200);
    uint8_t dec[256];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 256);
    ASSERT(memcmp(dec, in, 256) == 0);
}

TEST(compress_incompressible_data) {
    /* Random-ish data: should not crash, output may be >= input */
    uint8_t in[128];
    for (int i = 0; i < 128; i++) in[i] = (uint8_t)(i * 7 + 13);
    uint8_t out[200];
    int result = lz4_compress_block(in, 128, out, sizeof(out));
    ASSERT(result > 0);
    uint8_t dec[128];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 128);
    ASSERT(memcmp(dec, in, 128) == 0);
}

TEST(compress_output_buffer_too_small) {
    uint8_t in[64];
    memset(in, 0, 64);
    uint8_t out[2];  /* Way too small */
    int result = lz4_compress_block(in, 64, out, sizeof(out));
    ASSERT_EQ(result, -1);
}

TEST(compress_typical_c64_basic) {
    /* Simulate BASIC program: lots of zeros + some tokens */
    uint8_t in[256];
    memset(in, 0x00, 256);
    /* BASIC tokens scattered */
    in[0] = 0x01; in[1] = 0x08;  /* Load address $0801 */
    in[2] = 0x0A; in[3] = 0x08;  /* Next line pointer */
    in[10] = 0x99;               /* PRINT token */
    in[20] = 0x00;               /* End of line */
    uint8_t out[300];
    int result = lz4_compress_block(in, 256, out, sizeof(out));
    ASSERT(result > 0);
    ASSERT(result < 128);  /* Should compress well (lots of zeros) */
    uint8_t dec[256];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 256);
    ASSERT(memcmp(dec, in, 256) == 0);
}

TEST(compress_max_block_size) {
    /* 256 bytes = protocol block size */
    uint8_t in[256];
    for (int i = 0; i < 256; i++) in[i] = (uint8_t)(i & 0xFF);
    uint8_t out[512];
    int result = lz4_compress_block(in, 256, out, sizeof(out));
    ASSERT(result > 0);
    uint8_t dec[256];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 256);
    ASSERT(memcmp(dec, in, 256) == 0);
}

TEST(compress_long_match) {
    /* Pattern that creates a long match (>= 19 bytes, triggers extension) */
    uint8_t in[256];
    memcpy(in, "HELLO WORLD! THIS IS A TEST. ", 29);
    memcpy(in + 29, "HELLO WORLD! THIS IS A TEST. ", 29);
    memcpy(in + 58, "HELLO WORLD! THIS IS A TEST. ", 29);
    memset(in + 87, 'X', 256 - 87);
    uint8_t out[300];
    int result = lz4_compress_block(in, 256, out, sizeof(out));
    ASSERT(result > 0);
    uint8_t dec[256];
    int dec_len = lz4_decompress_block(out, result, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 256);
    ASSERT(memcmp(dec, in, 256) == 0);
}

/* ---- Main ---- */

int main(void) {
    printf("=== Nano LZ4 Compression Unit Tests ===\n\n");

    printf("LZ4 block compression:\n");
    RUN(compress_empty_input);
    RUN(compress_single_byte);
    RUN(compress_short_literal_only);
    RUN(compress_repeated_data);
    RUN(compress_repeated_pattern);
    RUN(compress_incompressible_data);
    RUN(compress_output_buffer_too_small);
    RUN(compress_typical_c64_basic);
    RUN(compress_max_block_size);
    RUN(compress_long_match);

    printf("\n========================================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
```

**Step 2: Add test to Makefile**

In `test/Makefile`, add build rule and test target:

```makefile
# After test_nano_shiftreg rule, add:
test_nano_lz4: test_nano_lz4.c $(NANO_SRC)/lz4_compress.c
	$(CC) $(CFLAGS) $(NANO_INCLUDES) -o $@ $^
```

Add `test_nano_lz4` to the `all:` and `test:` targets and `clean:` rule.

**Step 3: Verify tests don't compile (source doesn't exist yet)**

```bash
cd test && make test_nano_lz4
```
Expected: compilation error — `lz4_compress.h: No such file`

**Step 4: Commit**

```bash
git add test/test_nano_lz4.c test/Makefile
git commit -m "test: add LZ4 compression unit tests"
```

---

### Task 2: LZ4 block compressor — implementation

**Files:**
- Create: `firmware/E-IEC-Nano-SRAM/include/lz4_compress.h`
- Create: `firmware/E-IEC-Nano-SRAM/src/lz4_compress.c`

Implement minimal LZ4 block compressor optimized for ATmega328P.

**Step 1: Write header**

```c
#ifndef LZ4_COMPRESS_H
#define LZ4_COMPRESS_H

#include <stdint.h>

/*
 * LZ4 block compressor for 64korppu.
 *
 * Compresses a raw data block into LZ4 block format (no frame header).
 * Optimized for small blocks (256 bytes) on ATmega328P.
 *
 * Uses a 256-entry hash table (512 bytes) on the stack during compression.
 */

/* Compress src_len bytes from src into dst.
 *
 * Returns compressed size on success (may be >= src_len if incompressible).
 * Returns -1 if dst_cap is too small.
 * Returns 0 if src_len is 0.
 */
int lz4_compress_block(const uint8_t *src, int src_len,
                       uint8_t *dst, int dst_cap);

#endif /* LZ4_COMPRESS_H */
```

**Step 2: Write implementation**

```c
#include "lz4_compress.h"
#include <string.h>

/*
 * Minimal LZ4 block compressor.
 *
 * Hash table: 256 entries × 2 bytes = 512 bytes on stack.
 * Matches minimum 4 bytes, max offset 65535.
 * Produces standard LZ4 block format compatible with any LZ4 decoder.
 */

#define LZ4_HASH_BITS   8
#define LZ4_HASH_SIZE   (1 << LZ4_HASH_BITS)
#define LZ4_MIN_MATCH   4
#define LZ4_LAST_LITERALS 5  /* LZ4 spec: last 5 bytes are always literals */

static uint8_t lz4_hash(const uint8_t *p) {
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                 ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return (uint8_t)((v * 2654435761U) >> 24);
}

/* Write a variable-length count (used for literal and match lengths > 14) */
static uint8_t *lz4_write_count(uint8_t *dst, uint8_t *dst_end, int count) {
    while (count >= 255) {
        if (dst >= dst_end) return NULL;
        *dst++ = 255;
        count -= 255;
    }
    if (dst >= dst_end) return NULL;
    *dst++ = (uint8_t)count;
    return dst;
}

int lz4_compress_block(const uint8_t *src, int src_len,
                       uint8_t *dst, int dst_cap) {
    if (src_len == 0) return 0;

    uint16_t htable[LZ4_HASH_SIZE];
    memset(htable, 0, sizeof(htable));

    const uint8_t *sp = src;
    const uint8_t *se = src + src_len;
    const uint8_t *match_limit = se - LZ4_LAST_LITERALS;
    const uint8_t *anchor = sp;  /* Start of current literal run */

    uint8_t *dp = dst;
    uint8_t *de = dst + dst_cap;

    /* Skip first byte (need at least 4 bytes for match) */
    if (src_len < LZ4_MIN_MATCH + LZ4_LAST_LITERALS) {
        /* Too short for any match — emit all as literals */
        goto emit_last_literals;
    }
    sp++;

    while (sp < match_limit) {
        uint8_t h = lz4_hash(sp);
        uint16_t ref_idx = htable[h];
        const uint8_t *ref = src + ref_idx;
        htable[h] = (uint16_t)(sp - src);

        /* Check if match is valid */
        if (ref < src || sp - ref > 65535 ||
            ref + LZ4_MIN_MATCH > match_limit ||
            memcmp(ref, sp, LZ4_MIN_MATCH) != 0) {
            sp++;
            continue;
        }

        /* Found a match! */
        int lit_len = (int)(sp - anchor);
        int match_len = LZ4_MIN_MATCH;

        /* Extend match forward */
        while (sp + match_len < se - LZ4_LAST_LITERALS &&
               sp[match_len] == ref[match_len]) {
            match_len++;
        }

        /* Write token */
        if (dp >= de) return -1;
        uint8_t *token_ptr = dp++;
        int lit_code = lit_len < 15 ? lit_len : 15;
        int match_code = (match_len - LZ4_MIN_MATCH) < 15 ?
                         (match_len - LZ4_MIN_MATCH) : 15;
        *token_ptr = (uint8_t)((lit_code << 4) | match_code);

        /* Write literal length extension */
        if (lit_len >= 15) {
            dp = lz4_write_count(dp, de, lit_len - 15);
            if (!dp) return -1;
        }

        /* Write literals */
        if (dp + lit_len > de) return -1;
        memcpy(dp, anchor, lit_len);
        dp += lit_len;

        /* Write match offset (little-endian) */
        if (dp + 2 > de) return -1;
        uint16_t offset = (uint16_t)(sp - ref);
        *dp++ = (uint8_t)(offset & 0xFF);
        *dp++ = (uint8_t)(offset >> 8);

        /* Write match length extension */
        if (match_len - LZ4_MIN_MATCH >= 15) {
            dp = lz4_write_count(dp, de, match_len - LZ4_MIN_MATCH - 15);
            if (!dp) return -1;
        }

        /* Advance past match */
        sp += match_len;
        anchor = sp;
    }

emit_last_literals:;
    /* Emit remaining bytes as literals (last sequence, no match) */
    int last_lit = (int)(se - anchor);
    if (last_lit > 0) {
        if (dp >= de) return -1;
        uint8_t *token_ptr = dp++;
        int lit_code = last_lit < 15 ? last_lit : 15;
        *token_ptr = (uint8_t)(lit_code << 4);

        if (last_lit >= 15) {
            dp = lz4_write_count(dp, de, last_lit - 15);
            if (!dp) return -1;
        }

        if (dp + last_lit > de) return -1;
        memcpy(dp, anchor, last_lit);
        dp += last_lit;
    }

    return (int)(dp - dst);
}
```

**Step 3: Build and run tests**

```bash
cd test && make test_nano_lz4 && ./test_nano_lz4
```
Expected: all 10 tests PASS.

**Step 4: Commit**

```bash
git add firmware/E-IEC-Nano-SRAM/include/lz4_compress.h \
        firmware/E-IEC-Nano-SRAM/src/lz4_compress.c
git commit -m "feat(fw): add LZ4 block compressor for IEC transfer"
```

---

### Task 3: Compression protocol — tests

**Files:**
- Modify: `test/test_nano_lz4.c` (add protocol tests)

Add tests for XZ command parsing and block framing protocol.

**Step 1: Add protocol tests to test_nano_lz4.c**

Add these tests after the LZ4 compression tests, and add `#include "compress_proto.h"` at the top:

```c
/* ---- XZ command channel tests ---- */

TEST(xz_default_disabled) {
    compress_proto_init();
    ASSERT_EQ(compress_proto_enabled(), false);
}

TEST(xz_enable) {
    compress_proto_init();
    ASSERT(compress_proto_handle_command("XZ:1", 4) == true);
    ASSERT_EQ(compress_proto_enabled(), true);
}

TEST(xz_disable) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    ASSERT(compress_proto_handle_command("XZ:0", 4) == true);
    ASSERT_EQ(compress_proto_enabled(), false);
}

TEST(xz_status_query_disabled) {
    compress_proto_init();
    char buf[16];
    ASSERT(compress_proto_handle_command("XZ:S", 4) == true);
    int len = compress_proto_get_status(buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(memcmp(buf, "XZ:0", 4) == 0);
}

TEST(xz_status_query_enabled) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    char buf[16];
    compress_proto_handle_command("XZ:S", 4);
    int len = compress_proto_get_status(buf, sizeof(buf));
    ASSERT(len > 0);
    ASSERT(memcmp(buf, "XZ:1", 4) == 0);
}

TEST(xz_not_xz_command) {
    compress_proto_init();
    ASSERT(compress_proto_handle_command("S:FILE", 6) == false);
}

TEST(xz_invalid_subcommand) {
    compress_proto_init();
    ASSERT(compress_proto_handle_command("XZ:X", 4) == false);
}

TEST(xz_case_insensitive) {
    compress_proto_init();
    ASSERT(compress_proto_handle_command("xz:1", 4) == true);
    ASSERT_EQ(compress_proto_enabled(), true);
}

TEST(xz_reset_disables) {
    compress_proto_init();
    compress_proto_handle_command("XZ:1", 4);
    compress_proto_init();
    ASSERT_EQ(compress_proto_enabled(), false);
}

/* ---- Block framing tests ---- */

TEST(frame_block_header) {
    uint8_t raw[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t out[64];
    int result = compress_proto_frame_block(raw, 8, out, sizeof(out));
    ASSERT(result > 4);
    /* First 2 bytes: compressed size (little-endian) */
    uint16_t comp_size = out[0] | (out[1] << 8);
    /* Next 2 bytes: raw size (little-endian) */
    uint16_t raw_size = out[2] | (out[3] << 8);
    ASSERT_EQ(raw_size, 8);
    ASSERT_EQ(comp_size, result - 4);
    /* Verify payload decompresses correctly */
    uint8_t dec[16];
    int dec_len = lz4_decompress_block(out + 4, comp_size, dec, sizeof(dec));
    ASSERT_EQ(dec_len, 8);
    ASSERT(memcmp(dec, raw, 8) == 0);
}

TEST(frame_eof_marker) {
    uint8_t out[4];
    int result = compress_proto_frame_eof(out, sizeof(out));
    ASSERT_EQ(result, 2);
    ASSERT_EQ(out[0], 0);
    ASSERT_EQ(out[1], 0);
}

TEST(frame_256_byte_block) {
    uint8_t raw[256];
    memset(raw, 0x00, 256);
    uint8_t out[512];
    int result = compress_proto_frame_block(raw, 256, out, sizeof(out));
    ASSERT(result > 4);
    uint16_t raw_size = out[2] | (out[3] << 8);
    ASSERT_EQ(raw_size, 256);
    uint16_t comp_size = out[0] | (out[1] << 8);
    ASSERT(comp_size < 256);  /* Zeros compress well */
}
```

Add to `main()`:

```c
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

    printf("\nBlock framing:\n");
    RUN(frame_block_header);
    RUN(frame_eof_marker);
    RUN(frame_256_byte_block);
```

**Step 2: Update Makefile build rule**

```makefile
test_nano_lz4: test_nano_lz4.c $(NANO_SRC)/lz4_compress.c $(NANO_SRC)/compress_proto.c
	$(CC) $(CFLAGS) $(NANO_INCLUDES) -o $@ $^
```

**Step 3: Verify new tests don't compile**

```bash
cd test && make test_nano_lz4
```
Expected: error — `compress_proto.h: No such file`

**Step 4: Commit**

```bash
git add test/test_nano_lz4.c test/Makefile
git commit -m "test: add XZ command and block framing tests"
```

---

### Task 4: Compression protocol — implementation

**Files:**
- Create: `firmware/E-IEC-Nano-SRAM/include/compress_proto.h`
- Create: `firmware/E-IEC-Nano-SRAM/src/compress_proto.c`

**Step 1: Write header**

```c
#ifndef COMPRESS_PROTO_H
#define COMPRESS_PROTO_H

#include <stdint.h>
#include <stdbool.h>

/*
 * LZ4 compression protocol for 64korppu IEC transfer.
 *
 * Manages XZ command channel state and block framing.
 * See docs/E-IEC-Nano-SRAM/lz4-protokolla.md for protocol specification.
 */

/* Initialize compression state (disabled by default) */
void compress_proto_init(void);

/* Handle XZ command from command channel (SA 15).
 * Returns true if command was recognized as XZ command (even if invalid).
 * Returns false if not an XZ command (caller should try other parsers). */
bool compress_proto_handle_command(const char *cmd, uint8_t len);

/* Returns true if compression is currently enabled */
bool compress_proto_enabled(void);

/* Get status string for XZ:S query.
 * Writes "XZ:0" or "XZ:1" to buf. Returns length written. */
int compress_proto_get_status(char *buf, int buf_size);

/* Compress raw_len bytes and write framed block (4B header + LZ4 payload).
 * Returns total framed size, or -1 on error. */
int compress_proto_frame_block(const uint8_t *raw, int raw_len,
                               uint8_t *out, int out_cap);

/* Write EOF marker (2 bytes: 0x0000).
 * Returns 2 on success, -1 if buffer too small. */
int compress_proto_frame_eof(uint8_t *out, int out_cap);

#endif /* COMPRESS_PROTO_H */
```

**Step 2: Write implementation**

```c
#include "compress_proto.h"
#include "lz4_compress.h"
#include <string.h>
#include <ctype.h>

static bool compression_enabled = false;
static bool status_queried = false;

void compress_proto_init(void) {
    compression_enabled = false;
    status_queried = false;
}

bool compress_proto_handle_command(const char *cmd, uint8_t len) {
    if (len < 4) return false;

    /* Check for XZ: prefix (case-insensitive) */
    if (toupper((unsigned char)cmd[0]) != 'X') return false;
    if (toupper((unsigned char)cmd[1]) != 'Z') return false;
    if (cmd[2] != ':') return false;

    char sub = toupper((unsigned char)cmd[3]);
    switch (sub) {
        case '1':
            compression_enabled = true;
            return true;
        case '0':
            compression_enabled = false;
            return true;
        case 'S':
            status_queried = true;
            return true;
        default:
            return false;
    }
}

bool compress_proto_enabled(void) {
    return compression_enabled;
}

int compress_proto_get_status(char *buf, int buf_size) {
    const char *status = compression_enabled ? "XZ:1" : "XZ:0";
    int len = 4;
    if (buf_size < len) return -1;
    memcpy(buf, status, len);
    status_queried = false;
    return len;
}

int compress_proto_frame_block(const uint8_t *raw, int raw_len,
                               uint8_t *out, int out_cap) {
    if (raw_len <= 0 || out_cap < 6) return -1;

    /* Compress into space after 4-byte header */
    int comp_len = lz4_compress_block(raw, raw_len, out + 4, out_cap - 4);
    if (comp_len < 0) return -1;

    /* Write header: compressed_size (LE), raw_size (LE) */
    out[0] = (uint8_t)(comp_len & 0xFF);
    out[1] = (uint8_t)((comp_len >> 8) & 0xFF);
    out[2] = (uint8_t)(raw_len & 0xFF);
    out[3] = (uint8_t)((raw_len >> 8) & 0xFF);

    return 4 + comp_len;
}

int compress_proto_frame_eof(uint8_t *out, int out_cap) {
    if (out_cap < 2) return -1;
    out[0] = 0;
    out[1] = 0;
    return 2;
}
```

**Step 3: Build and run all tests**

```bash
cd test && make test_nano_lz4 && ./test_nano_lz4
```
Expected: all 22 tests PASS.

**Step 4: Run all existing tests to verify no regressions**

```bash
cd test && make test && echo "ALL OK"
```
Expected: all test suites pass.

**Step 5: Commit**

```bash
git add firmware/E-IEC-Nano-SRAM/include/compress_proto.h \
        firmware/E-IEC-Nano-SRAM/src/compress_proto.c
git commit -m "feat(fw): add XZ command channel and block framing protocol"
```

---

### Task 5: Integrate into CBM-DOS command parsing

**Files:**
- Modify: `firmware/E-IEC-Nano-SRAM/src/cbm_dos.c`
- Modify: `firmware/E-IEC-Nano-SRAM/src/main.c`

Wire the XZ command into the existing command parser and initialize compression state at boot.

**Step 1: Add include and XZ command to `cbm_dos_execute_command()`**

In `cbm_dos.c`, add `#include "compress_proto.h"` to includes.

In `cbm_dos_execute_command()`, add XZ check before the switch statement,
after the fastload protocol checks:

```c
    /* Check for compression protocol commands */
    if (compress_proto_handle_command(cmd_buf, cmd_len)) {
        if (compress_proto_enabled()) {
            iec_set_error(0, "OK COMPRESS ON", 0, 0);
        } else {
            iec_set_error(0, "OK", 0, 0);
        }
        return;
    }
```

If `XZ:S` was sent, the status response should go into the error channel.
Modify the block above to handle status query:

```c
    if (compress_proto_handle_command(cmd_buf, cmd_len)) {
        char status_buf[8];
        int slen = compress_proto_get_status(status_buf, sizeof(status_buf));
        if (slen > 0) {
            /* XZ:S was queried — respond with status */
            iec_set_error(0, status_buf, 0, 0);
        } else if (compress_proto_enabled()) {
            iec_set_error(0, "OK COMPRESS ON", 0, 0);
        } else {
            iec_set_error(0, "OK", 0, 0);
        }
        return;
    }
```

**Step 2: Add `compress_proto_init()` to `main.c`**

In `main.c`, add `#include "compress_proto.h"` and call `compress_proto_init()` before the main loop, after fastload initialization:

```c
    compress_proto_init();
    uart_puts("Compress OK\r\n");
```

**Step 3: Build firmware (verify compilation)**

```bash
cd firmware/E-IEC-Nano-SRAM && make
```
Expected: compiles without errors.

**Step 4: Commit**

```bash
git add firmware/E-IEC-Nano-SRAM/src/cbm_dos.c \
        firmware/E-IEC-Nano-SRAM/src/main.c
git commit -m "feat(fw): integrate XZ command into CBM-DOS command parser"
```

---

### Task 6: Integrate compressed talk loop

**Files:**
- Modify: `firmware/E-IEC-Nano-SRAM/src/cbm_dos.c`

Modify `cbm_dos_talk_byte()` to send compressed blocks when compression is enabled.
This is the core integration: instead of sending raw bytes, collect 256 bytes,
compress, and send the framed block byte-by-byte.

**Step 1: Add compressed talk state variables**

Add static state to `cbm_dos.c`:

```c
/* Compressed transfer state */
static uint8_t  comp_raw_buf[256];     /* Accumulates raw data */
static uint8_t  comp_frame_buf[300];   /* Framed LZ4 block output */
static uint16_t comp_raw_pos;          /* Bytes accumulated */
static uint16_t comp_frame_len;        /* Total framed block size */
static uint16_t comp_frame_pos;        /* Current send position in frame */
static bool     comp_filling;          /* true = accumulating, false = draining frame */
```

**Step 2: Create helper function `cbm_dos_talk_byte_compressed()`**

```c
static bool cbm_dos_talk_byte_compressed(uint8_t sa, uint8_t *byte, bool *eoi) {
    *eoi = false;

    /* If draining a framed block, send next byte */
    if (!comp_filling && comp_frame_pos < comp_frame_len) {
        *byte = comp_frame_buf[comp_frame_pos++];
        if (comp_frame_pos >= comp_frame_len) {
            comp_filling = true;  /* Block sent, start filling next */
            comp_raw_pos = 0;
        }
        return true;
    }

    /* Accumulate raw bytes */
    while (comp_raw_pos < 256) {
        uint8_t raw_byte;
        bool raw_eoi;
        bool ok;

        /* Read one raw byte from the underlying source */
        if (sa == IEC_SA_LOAD && dir_active) {
            if (dir_pos >= dir_len) break;
            sram_read(SRAM_DIR_BUF + dir_pos, &raw_byte, 1);
            dir_pos++;
            raw_eoi = (dir_pos >= dir_len);
        } else if (sa == IEC_SA_LOAD && channel_file.active) {
            int rc = fat12_read(&channel_file, &raw_byte, 1);
            if (rc != 1) break;
            raw_eoi = (channel_file.position >= channel_file.file_size);
        } else {
            break;
        }

        comp_raw_buf[comp_raw_pos++] = raw_byte;
        if (raw_eoi) break;
    }

    if (comp_raw_pos == 0) {
        /* No more data — send EOF marker */
        int eof_len = compress_proto_frame_eof(comp_frame_buf, sizeof(comp_frame_buf));
        comp_frame_len = (uint16_t)eof_len;
        comp_frame_pos = 0;
        comp_filling = false;
        *byte = comp_frame_buf[comp_frame_pos++];
        if (comp_frame_pos >= comp_frame_len) {
            *eoi = true;
        }
        return true;
    }

    /* Compress and frame the block */
    int framed = compress_proto_frame_block(comp_raw_buf, comp_raw_pos,
                                            comp_frame_buf, sizeof(comp_frame_buf));
    if (framed < 0) {
        /* Compression failed — should not happen, but handle gracefully */
        *eoi = true;
        return false;
    }
    comp_frame_len = (uint16_t)framed;
    comp_frame_pos = 0;
    comp_filling = false;

    /* Check if this was the last data (source exhausted with less than 256 bytes) */
    bool is_last = (comp_raw_pos < 256);

    if (is_last) {
        /* Append EOF after this block */
        int eof_len = compress_proto_frame_eof(
            comp_frame_buf + comp_frame_len,
            sizeof(comp_frame_buf) - comp_frame_len);
        if (eof_len > 0) {
            comp_frame_len += (uint16_t)eof_len;
        }
    }

    /* Send first byte of frame */
    *byte = comp_frame_buf[comp_frame_pos++];
    if (comp_frame_pos >= comp_frame_len && is_last) {
        *eoi = true;
    }
    return true;
}
```

**Step 3: Modify `cbm_dos_talk_byte()` to dispatch**

Replace the beginning of `cbm_dos_talk_byte()`:

```c
bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi) {
    /* Use compressed transfer if enabled and not command channel */
    if (compress_proto_enabled() && sa != IEC_SA_COMMAND) {
        return cbm_dos_talk_byte_compressed(sa, byte, eoi);
    }

    /* Original uncompressed code below... */
```

**Step 4: Initialize compressed state in `cbm_dos_open()`**

When a file or directory is opened, reset the compression state:

```c
    /* Reset compression buffer state */
    comp_raw_pos = 0;
    comp_frame_len = 0;
    comp_frame_pos = 0;
    comp_filling = true;
```

**Step 5: Build firmware**

```bash
cd firmware/E-IEC-Nano-SRAM && make
```
Expected: compiles without errors.

**Step 6: Commit**

```bash
git add firmware/E-IEC-Nano-SRAM/src/cbm_dos.c
git commit -m "feat(fw): add compressed talk loop with LZ4 block framing"
```

---

### Task 7: Update Makefile and config

**Files:**
- Modify: `firmware/E-IEC-Nano-SRAM/Makefile`
- Modify: `firmware/E-IEC-Nano-SRAM/include/config.h`

**Step 1: Add new source files to firmware Makefile**

Add `lz4_compress.c` and `compress_proto.c` to the `SRC` variable in the firmware Makefile.

**Step 2: Add compression config to config.h**

```c
/* --- LZ4 Compression --- */

#define COMPRESS_BLOCK_SIZE     256     /* Raw bytes per compression block */
#define COMPRESS_FRAME_BUF_SIZE 300     /* Max framed block (header + LZ4) */
```

**Step 3: Build firmware and run all tests**

```bash
cd firmware/E-IEC-Nano-SRAM && make
cd ../../test && make clean && make test
```
Expected: firmware compiles, all tests pass.

**Step 4: Commit**

```bash
git add firmware/E-IEC-Nano-SRAM/Makefile \
        firmware/E-IEC-Nano-SRAM/include/config.h
git commit -m "feat(fw): add LZ4 sources to Makefile, add compression config"
```

---

### Task 8: Run all tests and final verification

**Step 1: Clean build everything**

```bash
cd test && make clean && make all
```

**Step 2: Run all tests**

```bash
cd test && make test
```
Expected: all test suites pass including new LZ4 tests.

**Step 3: Build firmware for target**

```bash
cd firmware/E-IEC-Nano-SRAM && make clean && make
```
Expected: compiles, note flash and RAM usage in output.

**Step 4: Verify flash/RAM budget**

Check that flash usage is < 50% and RAM < 80%.

**Step 5: Final commit if any fixes needed**

```bash
git add -A && git status
# If clean, no commit needed
# If fixes: git commit -m "fix(fw): address build/test issues in LZ4 integration"
```
