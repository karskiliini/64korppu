/*
 * Comprehensive unit tests for the D64 disk image module.
 *
 * Runs on the host (Linux/macOS) — no Pico hardware needed.
 * Builds against d64.c with mock_fat12.c providing FAT12 stubs.
 *
 * Usage:
 *   make test
 *   ./test_d64
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "d64.h"
#include "mock_fat12.h"

/* ---- Test framework ---- */

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

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("FAIL\n    Expected \"%s\" == \"%s\"\n    at %s:%d\n", \
               (a), (b), __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

/* ---- D64 image generator ---- */

/* Sectors per track (same as in d64.c) */
static const uint8_t spt[36] = {
    0,
    21,21,21,21,21,21,21,21,21,21,  /* 1-10 */
    21,21,21,21,21,21,21,            /* 11-17 */
    19,19,19,19,19,19,19,            /* 18-24 */
    18,18,18,18,18,18,               /* 25-30 */
    17,17,17,17,17                   /* 31-35 */
};

static int32_t test_ts_offset(uint8_t track, uint8_t sector) {
    if (track < 1 || track > 35) return -1;
    if (sector >= spt[track]) return -1;
    int32_t off = 0;
    for (uint8_t t = 1; t < track; t++) off += spt[t] * 256;
    off += sector * 256;
    return off;
}

static uint8_t *test_sector(uint8_t *image, uint8_t track, uint8_t sector) {
    int32_t off = test_ts_offset(track, sector);
    if (off < 0) return NULL;
    return &image[off];
}

/*
 * Create a valid empty D64 image with proper BAM.
 */
static void create_empty_d64(uint8_t *image, const char *disk_name) {
    memset(image, 0, D64_IMAGE_SIZE);

    uint8_t *bam = test_sector(image, 18, 0);

    /* First directory sector location */
    bam[0] = 18;  /* Track */
    bam[1] = 1;   /* Sector */
    bam[2] = 0x41; /* DOS version 'A' */
    bam[3] = 0x00;

    /* Initialize BAM entries for all 35 tracks */
    for (uint8_t t = 1; t <= 35; t++) {
        uint8_t offset = 4 * t;
        uint8_t nsec = spt[t];

        /* Build bitmap: bit=1 means free */
        uint8_t b0 = 0, b1 = 0, b2 = 0;
        for (uint8_t s = 0; s < nsec; s++) {
            if (s < 8)  b0 |= (1 << s);
            else if (s < 16) b1 |= (1 << (s - 8));
            else b2 |= (1 << (s - 16));
        }

        bam[offset + 0] = nsec;  /* Free sector count */
        bam[offset + 1] = b0;
        bam[offset + 2] = b1;
        bam[offset + 3] = b2;
    }

    /* Allocate track 18, sector 0 (BAM) and sector 1 (first dir) */
    /* Track 18 BAM entry at offset 4*18=72 */
    bam[72] -= 2;  /* 19 - 2 = 17 free */
    bam[73] &= ~0x03;  /* Clear bits 0 and 1 (sectors 0 and 1) */

    /* Disk name at offset 0x90 (padded with 0xA0) */
    memset(&bam[0x90], 0xA0, 16);
    int nlen = strlen(disk_name);
    if (nlen > 16) nlen = 16;
    memcpy(&bam[0x90], disk_name, nlen);

    /* Disk ID and other fields */
    bam[0xA0] = 0xA0;
    bam[0xA1] = 0xA0;
    bam[0xA2] = '6';   /* Disk ID byte 1 */
    bam[0xA3] = '4';   /* Disk ID byte 2 */
    bam[0xA4] = 0xA0;
    bam[0xA5] = '2';   /* DOS type */
    bam[0xA6] = 'A';
    bam[0xA7] = 0xA0;
    bam[0xA8] = 0xA0;
    bam[0xA9] = 0xA0;
    bam[0xAA] = 0xA0;

    /* First directory sector: empty, no next sector */
    uint8_t *dir = test_sector(image, 18, 1);
    dir[0] = 0;  /* No next directory sector */
    dir[1] = 0xFF; /* Standard: 0xFF when no more sectors */
}

/*
 * Allocate a sector in the BAM.
 */
static void bam_allocate(uint8_t *image, uint8_t track, uint8_t sector) {
    uint8_t *bam = test_sector(image, 18, 0);
    uint8_t offset = 4 * track;
    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    if (bam[offset + byte_idx] & bit) {
        bam[offset + byte_idx] &= ~bit;
        bam[offset]--;
    }
}

/*
 * Add a PRG file to the D64 image.
 * Returns true on success.
 */
static bool add_file_to_d64(uint8_t *image, const char *name,
                              const uint8_t *data, uint16_t data_size) {
    /* Find free directory entry in track 18, sector 1 */
    uint8_t *dir = test_sector(image, 18, 1);

    int dir_slot = -1;
    for (int i = 0; i < 8; i++) {
        uint8_t *entry = &dir[i * 32];
        if (entry[2] == 0x00) {  /* Empty slot */
            dir_slot = i;
            break;
        }
    }
    if (dir_slot < 0) return false;

    /* Allocate data blocks and write data */
    /* Find free sectors starting from track 17, going outward */
    uint8_t *bam = test_sector(image, 18, 0);

    uint8_t first_t = 0, first_s = 0;
    uint8_t prev_t = 0, prev_s = 0;
    uint16_t offset = 0;
    uint16_t blocks = 0;

    while (offset < data_size) {
        /* Find a free sector */
        uint8_t alloc_t = 0, alloc_s = 0;
        for (uint8_t t = 1; t <= 35; t++) {
            if (t == 18) continue;
            for (uint8_t s = 0; s < spt[t]; s++) {
                uint8_t byte_idx = 1 + (s / 8);
                uint8_t bit = 1 << (s % 8);
                if (bam[4 * t + byte_idx] & bit) {
                    alloc_t = t;
                    alloc_s = s;
                    goto found;
                }
            }
        }
        return false;  /* Disk full */

    found:
        bam_allocate(image, alloc_t, alloc_s);

        if (first_t == 0) {
            first_t = alloc_t;
            first_s = alloc_s;
        }

        /* Link previous block to this one */
        if (prev_t != 0) {
            uint8_t *prev = test_sector(image, prev_t, prev_s);
            prev[0] = alloc_t;
            prev[1] = alloc_s;
        }

        /* Write data to this block */
        uint8_t *block = test_sector(image, alloc_t, alloc_s);
        uint16_t chunk = data_size - offset;
        if (chunk > 254) chunk = 254;

        memcpy(&block[2], &data[offset], chunk);

        if (offset + chunk >= data_size) {
            /* Last block */
            block[0] = 0;
            block[1] = chunk + 1;  /* Last byte position */
        } else {
            block[0] = 0;  /* Will be filled by next iteration */
            block[1] = 0;
        }

        prev_t = alloc_t;
        prev_s = alloc_s;
        offset += chunk;
        blocks++;
    }

    /* Fill directory entry */
    uint8_t *entry = &dir[dir_slot * 32];
    memset(entry, 0, 32);
    entry[2] = 0x82;  /* PRG + closed */
    entry[3] = first_t;
    entry[4] = first_s;

    /* Filename padded with 0xA0 */
    memset(&entry[5], 0xA0, 16);
    int nlen = strlen(name);
    if (nlen > 16) nlen = 16;
    memcpy(&entry[5], name, nlen);

    /* Size in blocks */
    entry[30] = blocks & 0xFF;
    entry[31] = (blocks >> 8) & 0xFF;

    return true;
}

/*
 * Register a D64 image in mock FAT12 as "TEST    .D64".
 */
static void register_d64(uint8_t *image) {
    mock_fat12_reset();
    mock_fat12_add_file("TEST    ", "D64", image, D64_IMAGE_SIZE);
}

/* ---- Tests ---- */

/* --- Image size and geometry --- */

TEST(image_size) {
    ASSERT_EQ(D64_IMAGE_SIZE, 174848);
    ASSERT_EQ(D64_SECTOR_SIZE, 256);
    ASSERT_EQ(D64_TOTAL_SECTORS, 683);

    /* Verify total sectors: sum of all tracks */
    int total = 0;
    for (int t = 1; t <= 35; t++) total += spt[t];
    ASSERT_EQ(total, 683);
}

/* --- Mount / unmount --- */

TEST(mount_empty_d64) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "TEST DISK");
    register_d64(image);

    ASSERT(!d64_is_mounted());
    ASSERT(d64_mount("TEST.D64"));
    ASSERT(d64_is_mounted());
    ASSERT(!d64_is_dirty());

    char name[17];
    d64_get_disk_name(name);
    ASSERT_STR_EQ(name, "TEST DISK");

    d64_unmount();
    ASSERT(!d64_is_mounted());
}

TEST(mount_wrong_size) {
    uint8_t bad_data[1024];
    memset(bad_data, 0, sizeof(bad_data));

    mock_fat12_reset();
    mock_fat12_add_file("BAD     ", "D64", bad_data, sizeof(bad_data));

    ASSERT(!d64_mount("BAD.D64"));
    ASSERT(!d64_is_mounted());
}

TEST(mount_not_found) {
    mock_fat12_reset();
    ASSERT(!d64_mount("NOPE.D64"));
}

TEST(double_mount) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "DISK ONE");
    register_d64(image);

    ASSERT(d64_mount("TEST.D64"));

    /* Create second image and mount it (should unmount first) */
    create_empty_d64(image, "DISK TWO");
    mock_fat12_reset();
    mock_fat12_add_file("TWO     ", "D64", image, D64_IMAGE_SIZE);

    ASSERT(d64_mount("TWO.D64"));
    ASSERT(d64_is_mounted());

    char name[17];
    d64_get_disk_name(name);
    ASSERT_STR_EQ(name, "DISK TWO");

    d64_unmount();
}

/* --- Sector read/write --- */

TEST(read_sector_valid) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "SECTOR TEST");
    register_d64(image);

    ASSERT(d64_mount("TEST.D64"));

    uint8_t buf[256];
    ASSERT(d64_read_sector(18, 0, buf));
    /* BAM should have track 18 sector 1 pointer */
    ASSERT_EQ(buf[0], 18);
    ASSERT_EQ(buf[1], 1);
    ASSERT_EQ(buf[2], 0x41);  /* 'A' */

    d64_unmount();
}

TEST(read_sector_invalid) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "TEST");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    uint8_t buf[256];
    ASSERT(!d64_read_sector(0, 0, buf));    /* Track 0 invalid */
    ASSERT(!d64_read_sector(36, 0, buf));   /* Track 36 invalid */
    ASSERT(!d64_read_sector(1, 21, buf));   /* Track 1 has 0-20 */
    ASSERT(!d64_read_sector(18, 19, buf));  /* Track 18 has 0-18 */

    d64_unmount();
}

TEST(write_sector_sets_dirty) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "WRITE TEST");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    ASSERT(!d64_is_dirty());

    uint8_t buf[256];
    memset(buf, 0xAA, 256);
    ASSERT(d64_write_sector(1, 0, buf));
    ASSERT(d64_is_dirty());

    /* Read it back */
    uint8_t buf2[256];
    ASSERT(d64_read_sector(1, 0, buf2));
    ASSERT(memcmp(buf, buf2, 256) == 0);

    d64_unmount();
}

/* --- Directory listing --- */

TEST(dir_empty_disk) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "EMPTY");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_dir_entry_t entry;
    ASSERT_EQ(d64_dir_first(&entry), -1);  /* No files */

    d64_unmount();
}

TEST(dir_with_files) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "FILES");

    /* Add two PRG files */
    uint8_t prg1[] = {0x01, 0x08, 0xAA, 0xBB, 0xCC};  /* load addr $0801 + data */
    uint8_t prg2[600];
    memset(prg2, 0x55, sizeof(prg2));
    prg2[0] = 0x01; prg2[1] = 0x08;

    ASSERT(add_file_to_d64(image, "HELLO", prg1, sizeof(prg1)));
    ASSERT(add_file_to_d64(image, "BIGPROG", prg2, sizeof(prg2)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_dir_entry_t entry;
    ASSERT_EQ(d64_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "HELLO");
    ASSERT_EQ(entry.file_type, D64_FILE_PRG);
    ASSERT_EQ(entry.size_blocks, 1);

    ASSERT_EQ(d64_dir_next(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "BIGPROG");
    ASSERT_EQ(entry.file_type, D64_FILE_PRG);
    /* 600 bytes = 3 blocks (254+254+92) */
    ASSERT_EQ(entry.size_blocks, 3);

    /* No more entries */
    ASSERT_EQ(d64_dir_next(&entry), -1);

    d64_unmount();
}

/* --- File reading --- */

TEST(read_small_prg) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "READTEST");

    uint8_t prg[] = {0x01, 0x08, 'H', 'E', 'L', 'L', 'O'};
    ASSERT(add_file_to_d64(image, "HELLO", prg, sizeof(prg)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("HELLO", &handle), 0);
    ASSERT(handle.active);
    ASSERT(!handle.eof);

    /* Read all bytes */
    for (int i = 0; i < (int)sizeof(prg); i++) {
        int val = d64_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, prg[i]);
    }

    /* Should be at EOF now */
    ASSERT(handle.eof);
    ASSERT_EQ(d64_file_read_byte(&handle), -1);

    d64_file_close(&handle);
    d64_unmount();
}

TEST(read_multiblock_prg) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "MULTI");

    /* Create a file that spans multiple blocks (>254 bytes data) */
    uint8_t prg[700];
    for (int i = 0; i < 700; i++) prg[i] = (uint8_t)(i & 0xFF);
    prg[0] = 0x01; prg[1] = 0x08;

    ASSERT(add_file_to_d64(image, "BIGFILE", prg, sizeof(prg)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("BIGFILE", &handle), 0);

    /* Read all bytes and verify */
    for (int i = 0; i < 700; i++) {
        int val = d64_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, prg[i]);
    }

    ASSERT(handle.eof);
    d64_file_close(&handle);
    d64_unmount();
}

TEST(read_file_not_found) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "TEST");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("NOPE", &handle), -1);

    d64_unmount();
}

TEST(read_exact_block_boundary) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "BOUNDARY");

    /* Exactly 254 bytes = one full data block */
    uint8_t prg[254];
    for (int i = 0; i < 254; i++) prg[i] = (uint8_t)i;

    ASSERT(add_file_to_d64(image, "EXACT254", prg, sizeof(prg)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("EXACT254", &handle), 0);

    for (int i = 0; i < 254; i++) {
        int val = d64_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, (uint8_t)i);
    }

    ASSERT(handle.eof);
    d64_file_close(&handle);
    d64_unmount();
}

/* --- File writing --- */

TEST(write_small_file) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "WRITE");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_create("NEWFILE", &handle), 0);
    ASSERT(handle.active);
    ASSERT(handle.write_mode);

    /* Write some bytes */
    uint8_t data[] = {0x01, 0x08, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < (int)sizeof(data); i++) {
        ASSERT_EQ(d64_file_write_byte(&handle, data[i]), 0);
    }

    d64_file_close(&handle);
    ASSERT(d64_is_dirty());

    /* Verify: find file in directory */
    d64_dir_entry_t entry;
    ASSERT_EQ(d64_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "NEWFILE");
    ASSERT_EQ(entry.file_type, D64_FILE_PRG);
    ASSERT_EQ(entry.size_blocks, 1);

    /* Read it back */
    d64_file_handle_t rh;
    ASSERT_EQ(d64_file_open("NEWFILE", &rh), 0);
    for (int i = 0; i < (int)sizeof(data); i++) {
        int val = d64_file_read_byte(&rh);
        ASSERT_EQ(val, data[i]);
    }
    ASSERT(rh.eof);
    d64_file_close(&rh);

    d64_unmount();
}

TEST(write_multiblock_file) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "BIGWRITE");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_create("BIGDATA", &handle), 0);

    /* Write 600 bytes (spans 3 blocks) */
    uint8_t data[600];
    for (int i = 0; i < 600; i++) data[i] = (uint8_t)(i & 0xFF);

    for (int i = 0; i < 600; i++) {
        ASSERT_EQ(d64_file_write_byte(&handle, data[i]), 0);
    }

    d64_file_close(&handle);

    /* Read back and verify */
    d64_file_handle_t rh;
    ASSERT_EQ(d64_file_open("BIGDATA", &rh), 0);

    for (int i = 0; i < 600; i++) {
        int val = d64_file_read_byte(&rh);
        ASSERT(val >= 0);
        ASSERT_EQ(val, data[i]);
    }
    ASSERT(rh.eof);
    d64_file_close(&rh);

    d64_unmount();
}

/* --- File deletion --- */

TEST(delete_file) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "DELETE");

    uint8_t prg[] = {0x01, 0x08, 0x42};
    ASSERT(add_file_to_d64(image, "DELME", prg, sizeof(prg)));
    ASSERT(add_file_to_d64(image, "KEEPME", prg, sizeof(prg)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Verify both exist */
    d64_dir_entry_t entry;
    ASSERT_EQ(d64_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "DELME");
    ASSERT_EQ(d64_dir_next(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "KEEPME");

    /* Delete DELME */
    ASSERT_EQ(d64_file_delete("DELME"), 0);
    ASSERT(d64_is_dirty());

    /* Only KEEPME should remain */
    ASSERT_EQ(d64_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "KEEPME");
    ASSERT_EQ(d64_dir_next(&entry), -1);

    /* Can't open deleted file */
    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("DELME", &handle), -1);

    d64_unmount();
}

TEST(delete_not_found) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "TEST");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    ASSERT_EQ(d64_file_delete("NOPE"), -1);

    d64_unmount();
}

/* --- Flush / write-back --- */

TEST(flush_writes_back) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "FLUSH");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Write a file */
    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_create("SAVED", &handle), 0);
    for (int i = 0; i < 10; i++) {
        d64_file_write_byte(&handle, (uint8_t)i);
    }
    d64_file_close(&handle);
    ASSERT(d64_is_dirty());

    /* Flush */
    ASSERT(d64_flush());
    ASSERT(!d64_is_dirty());

    /* Verify the data was written back to mock FAT12 */
    uint8_t readback[D64_IMAGE_SIZE];
    uint32_t size = mock_fat12_get_file("TEST    ", "D64", readback, sizeof(readback));
    ASSERT_EQ(size, D64_IMAGE_SIZE);

    d64_unmount();
}

TEST(unmount_auto_flushes) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "AUTOFLUSH");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Write a file */
    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_create("DATA", &handle), 0);
    d64_file_write_byte(&handle, 0x42);
    d64_file_close(&handle);
    ASSERT(d64_is_dirty());

    /* Unmount should auto-flush */
    d64_unmount();

    /* Verify written to mock */
    uint8_t readback[D64_IMAGE_SIZE];
    uint32_t size = mock_fat12_get_file("TEST    ", "D64", readback, sizeof(readback));
    ASSERT_EQ(size, D64_IMAGE_SIZE);
}

/* --- BAM integrity --- */

TEST(bam_updated_after_write) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "BAM TEST");

    /* Count initial free blocks */
    uint8_t *bam = test_sector(image, 18, 0);
    uint16_t initial_free = 0;
    for (uint8_t t = 1; t <= 35; t++) {
        if (t == 18) continue;
        initial_free += bam[4 * t];
    }

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Write a file that uses 1 block */
    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_create("SMALL", &handle), 0);
    d64_file_write_byte(&handle, 0x42);
    d64_file_close(&handle);

    /* Read BAM and count free blocks */
    uint8_t bam_buf[256];
    ASSERT(d64_read_sector(18, 0, bam_buf));
    uint16_t new_free = 0;
    for (uint8_t t = 1; t <= 35; t++) {
        if (t == 18) continue;
        new_free += bam_buf[4 * t];
    }

    /* Should have one fewer free block */
    ASSERT_EQ(new_free, initial_free - 1);

    d64_unmount();
}

TEST(bam_restored_after_delete) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "BAM DEL");

    uint8_t *bam = test_sector(image, 18, 0);
    uint16_t initial_free = 0;
    for (uint8_t t = 1; t <= 35; t++) {
        if (t == 18) continue;
        initial_free += bam[4 * t];
    }

    uint8_t prg[] = {0x01, 0x08, 0xAA};
    ASSERT(add_file_to_d64(image, "DELTEST", prg, sizeof(prg)));

    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Delete the file */
    ASSERT_EQ(d64_file_delete("DELTEST"), 0);

    /* BAM should be back to initial */
    uint8_t bam_buf[256];
    ASSERT(d64_read_sector(18, 0, bam_buf));
    uint16_t restored_free = 0;
    for (uint8_t t = 1; t <= 35; t++) {
        if (t == 18) continue;
        restored_free += bam_buf[4 * t];
    }
    ASSERT_EQ(restored_free, initial_free);

    d64_unmount();
}

/* --- Disk name --- */

TEST(disk_name_long) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "0123456789ABCDEF");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    char name[17];
    d64_get_disk_name(name);
    ASSERT_STR_EQ(name, "0123456789ABCDEF");

    d64_unmount();
}

TEST(disk_name_not_mounted) {
    char name[17] = "GARBAGE";
    d64_get_disk_name(name);
    ASSERT_EQ(name[0], 0);
}

/* --- Edge cases --- */

TEST(operations_when_not_mounted) {
    d64_dir_entry_t entry;
    ASSERT_EQ(d64_dir_first(&entry), -1);
    ASSERT_EQ(d64_dir_next(&entry), -1);

    d64_file_handle_t handle;
    ASSERT_EQ(d64_file_open("TEST", &handle), -1);
    ASSERT_EQ(d64_file_create("TEST", &handle), -1);
    ASSERT_EQ(d64_file_delete("TEST"), -1);

    ASSERT(!d64_is_mounted());
    ASSERT(!d64_is_dirty());
}

TEST(write_read_roundtrip) {
    /* Write a file via D64, unmount (flush), remount, read it back */
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "ROUNDTRIP");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Write */
    d64_file_handle_t wh;
    ASSERT_EQ(d64_file_create("MYDATA", &wh), 0);
    uint8_t test_data[100];
    for (int i = 0; i < 100; i++) test_data[i] = (uint8_t)(i * 3);
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(d64_file_write_byte(&wh, test_data[i]), 0);
    }
    d64_file_close(&wh);

    /* Unmount (auto-flush) */
    d64_unmount();

    /* Get the written D64 from mock FAT12 and re-register it */
    uint8_t written[D64_IMAGE_SIZE];
    uint32_t sz = mock_fat12_get_file("TEST    ", "D64", written, sizeof(written));
    ASSERT_EQ(sz, D64_IMAGE_SIZE);

    mock_fat12_reset();
    mock_fat12_add_file("TEST    ", "D64", written, D64_IMAGE_SIZE);

    /* Remount and read back */
    ASSERT(d64_mount("TEST.D64"));

    d64_file_handle_t rh;
    ASSERT_EQ(d64_file_open("MYDATA", &rh), 0);
    for (int i = 0; i < 100; i++) {
        int val = d64_file_read_byte(&rh);
        ASSERT_EQ(val, test_data[i]);
    }
    ASSERT(rh.eof);
    d64_file_close(&rh);

    d64_unmount();
}

TEST(multiple_files_write_and_read) {
    uint8_t image[D64_IMAGE_SIZE];
    create_empty_d64(image, "MULTI WR");
    register_d64(image);
    ASSERT(d64_mount("TEST.D64"));

    /* Write 3 files */
    for (int f = 0; f < 3; f++) {
        char name[17];
        snprintf(name, sizeof(name), "FILE%d", f);

        d64_file_handle_t wh;
        ASSERT_EQ(d64_file_create(name, &wh), 0);
        for (int i = 0; i < 50; i++) {
            d64_file_write_byte(&wh, (uint8_t)(f * 50 + i));
        }
        d64_file_close(&wh);
    }

    /* Verify directory */
    d64_dir_entry_t entry;
    int count = 0;
    int rc = d64_dir_first(&entry);
    while (rc == 0) {
        count++;
        rc = d64_dir_next(&entry);
    }
    ASSERT_EQ(count, 3);

    /* Read back each file */
    for (int f = 0; f < 3; f++) {
        char name[17];
        snprintf(name, sizeof(name), "FILE%d", f);

        d64_file_handle_t rh;
        ASSERT_EQ(d64_file_open(name, &rh), 0);
        for (int i = 0; i < 50; i++) {
            int val = d64_file_read_byte(&rh);
            ASSERT_EQ(val, (uint8_t)(f * 50 + i));
        }
        d64_file_close(&rh);
    }

    d64_unmount();
}

/* ---- Main ---- */

int main(void) {
    printf("=== D64 Unit Tests ===\n\n");

    printf("Image geometry:\n");
    RUN(image_size);

    printf("\nMount/Unmount:\n");
    RUN(mount_empty_d64);
    RUN(mount_wrong_size);
    RUN(mount_not_found);
    RUN(double_mount);

    printf("\nSector I/O:\n");
    RUN(read_sector_valid);
    RUN(read_sector_invalid);
    RUN(write_sector_sets_dirty);

    printf("\nDirectory listing:\n");
    RUN(dir_empty_disk);
    RUN(dir_with_files);

    printf("\nFile reading:\n");
    RUN(read_small_prg);
    RUN(read_multiblock_prg);
    RUN(read_file_not_found);
    RUN(read_exact_block_boundary);

    printf("\nFile writing:\n");
    RUN(write_small_file);
    RUN(write_multiblock_file);

    printf("\nFile deletion:\n");
    RUN(delete_file);
    RUN(delete_not_found);

    printf("\nFlush / write-back:\n");
    RUN(flush_writes_back);
    RUN(unmount_auto_flushes);

    printf("\nBAM integrity:\n");
    RUN(bam_updated_after_write);
    RUN(bam_restored_after_delete);

    printf("\nDisk name:\n");
    RUN(disk_name_long);
    RUN(disk_name_not_mounted);

    printf("\nEdge cases:\n");
    RUN(operations_when_not_mounted);
    RUN(write_read_roundtrip);
    RUN(multiple_files_write_and_read);

    printf("\n========================================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
