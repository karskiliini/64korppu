/*
 * Unit tests for the CBMFS (1581) filesystem module.
 *
 * Uses an in-memory 819200-byte buffer as mock disk I/O.
 * Tests cover: mount, directory, file read/write/delete, BAM, format.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "cbmfs.h"

/* ---- Test framework (same as test_d64.c) ---- */

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

/* ---- Mock I/O: in-memory disk image ---- */

static uint8_t *disk_image = NULL;

static int32_t logical_sector_offset(uint8_t track, uint8_t sector) {
    if (track < 1 || track > CBMFS_TRACKS) return -1;
    if (sector >= CBMFS_SECTORS_PER_TRACK) return -1;
    return ((track - 1) * CBMFS_SECTORS_PER_TRACK + sector) * CBMFS_SECTOR_SIZE;
}

static int mock_read_sector(void *ctx, uint8_t track, uint8_t sector, uint8_t *buf) {
    (void)ctx;
    int32_t off = logical_sector_offset(track, sector);
    if (off < 0) return -1;
    memcpy(buf, &disk_image[off], CBMFS_SECTOR_SIZE);
    return 0;
}

static int mock_write_sector(void *ctx, uint8_t track, uint8_t sector, const uint8_t *buf) {
    (void)ctx;
    int32_t off = logical_sector_offset(track, sector);
    if (off < 0) return -1;
    memcpy(&disk_image[off], buf, CBMFS_SECTOR_SIZE);
    return 0;
}

static cbmfs_io_t mock_io = {
    .read_sector = mock_read_sector,
    .write_sector = mock_write_sector,
    .context = NULL,
};

static void alloc_disk(void) {
    if (!disk_image) {
        disk_image = malloc(CBMFS_IMAGE_SIZE);
    }
    memset(disk_image, 0, CBMFS_IMAGE_SIZE);
}

static void format_disk(const char *name, const char *id) {
    alloc_disk();
    cbmfs_format(&mock_io, name, id);
}

/*
 * Add a file directly to the disk image for testing.
 */
static bool add_file(const char *name, const uint8_t *data, uint16_t data_size) {
    /* Mount, create, write, close, unmount */
    if (cbmfs_mount(&mock_io) != CBMFS_OK) return false;

    cbmfs_file_handle_t handle;
    if (cbmfs_file_create(name, &handle) != 0) {
        cbmfs_unmount();
        return false;
    }

    for (uint16_t i = 0; i < data_size; i++) {
        if (cbmfs_file_write_byte(&handle, data[i]) != 0) {
            cbmfs_file_close(&handle);
            cbmfs_unmount();
            return false;
        }
    }

    cbmfs_file_close(&handle);
    cbmfs_unmount();
    return true;
}

/* ---- Tests ---- */

/* --- Constants --- */

TEST(image_constants) {
    ASSERT_EQ(CBMFS_IMAGE_SIZE, 819200);
    ASSERT_EQ(CBMFS_SECTOR_SIZE, 256);
    ASSERT_EQ(CBMFS_TOTAL_SECTORS, 3200);
    ASSERT_EQ(CBMFS_TRACKS, 80);
    ASSERT_EQ(CBMFS_SECTORS_PER_TRACK, 40);
    ASSERT_EQ(CBMFS_DATA_BLOCKS, 3160);
}

/* --- Format --- */

TEST(format_creates_valid_disk) {
    format_disk("TEST DISK", "64");

    /* Verify header */
    uint8_t header[256];
    int32_t off = logical_sector_offset(40, 0);
    memcpy(header, &disk_image[off], 256);

    ASSERT_EQ(header[CBMFS_HDR_DIR_TRACK], 40);
    ASSERT_EQ(header[CBMFS_HDR_DIR_SECTOR], 3);
    ASSERT_EQ(header[CBMFS_HDR_DOS_VERSION], 0x44);
    ASSERT_EQ(header[CBMFS_HDR_DOS_TYPE], '3');
    ASSERT_EQ(header[CBMFS_HDR_DOS_TYPE + 1], 'D');

    /* Check disk name */
    ASSERT(memcmp(&header[CBMFS_HDR_DISK_NAME], "TEST DISK", 9) == 0);
    ASSERT_EQ(header[CBMFS_HDR_DISK_NAME + 9], 0xA0);

    /* Check disk ID */
    ASSERT_EQ(header[CBMFS_HDR_DISK_ID], '6');
    ASSERT_EQ(header[CBMFS_HDR_DISK_ID + 1], '4');
}

TEST(format_initializes_bam) {
    format_disk("BAM TEST", "AB");

    /* Read BAM sector 1 */
    uint8_t bam[256];
    int32_t off = logical_sector_offset(40, 1);
    memcpy(bam, &disk_image[off], 256);

    /* Check link to BAM sector 2 */
    ASSERT_EQ(bam[0], 40);
    ASSERT_EQ(bam[1], 2);
    ASSERT_EQ(bam[2], 0x44);  /* DOS version */
    ASSERT_EQ(bam[3], 0xBB);  /* Complementary flag */

    /* Track 1 should have 40 free sectors */
    uint16_t t1_off = CBMFS_BAM_DATA_OFFSET + 0;
    ASSERT_EQ(bam[t1_off], 40);
    ASSERT_EQ(bam[t1_off + 1], 0xFF);
    ASSERT_EQ(bam[t1_off + 2], 0xFF);
    ASSERT_EQ(bam[t1_off + 3], 0xFF);
    ASSERT_EQ(bam[t1_off + 4], 0xFF);
    ASSERT_EQ(bam[t1_off + 5], 0xFF);

    /* Track 40 should have 0 free sectors */
    uint16_t t40_off = CBMFS_BAM_DATA_OFFSET + 39 * CBMFS_BAM_BYTES_PER_TRACK;
    ASSERT_EQ(bam[t40_off], 0);
}

/* --- Mount/Unmount --- */

TEST(mount_formatted_disk) {
    format_disk("MOUNTED", "XY");

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);
    ASSERT(cbmfs_is_mounted());

    char name[17];
    cbmfs_get_disk_name(name);
    ASSERT_STR_EQ(name, "MOUNTED");

    char id[3];
    cbmfs_get_disk_id(id);
    ASSERT_STR_EQ(id, "XY");

    cbmfs_unmount();
    ASSERT(!cbmfs_is_mounted());
}

TEST(mount_invalid_disk) {
    alloc_disk();
    /* All zeros - should fail validation */
    ASSERT(cbmfs_mount(&mock_io) != CBMFS_OK);
    ASSERT(!cbmfs_is_mounted());
}

TEST(free_blocks_after_format) {
    format_disk("FREE TEST", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    /* 79 tracks × 40 sectors = 3160 (track 40 is system) */
    uint16_t free = cbmfs_free_blocks();
    ASSERT_EQ(free, 3160);

    cbmfs_unmount();
}

/* --- Directory --- */

TEST(dir_empty_disk) {
    format_disk("EMPTY", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_dir_entry_t entry;
    ASSERT_EQ(cbmfs_dir_first(&entry), -1);

    cbmfs_unmount();
}

TEST(dir_with_files) {
    format_disk("FILES", "AB");

    uint8_t prg1[] = {0x01, 0x08, 0xAA, 0xBB, 0xCC};
    ASSERT(add_file("HELLO", prg1, sizeof(prg1)));

    uint8_t prg2[600];
    memset(prg2, 0x55, sizeof(prg2));
    prg2[0] = 0x01; prg2[1] = 0x08;
    ASSERT(add_file("BIGPROG", prg2, sizeof(prg2)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_dir_entry_t entry;
    ASSERT_EQ(cbmfs_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "HELLO");
    ASSERT_EQ(entry.file_type, CBMFS_FILE_PRG);
    ASSERT_EQ(entry.size_blocks, 1);

    ASSERT_EQ(cbmfs_dir_next(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "BIGPROG");
    ASSERT_EQ(entry.file_type, CBMFS_FILE_PRG);
    /* 600 bytes = ceil(600/254) = 3 blocks */
    ASSERT_EQ(entry.size_blocks, 3);

    ASSERT_EQ(cbmfs_dir_next(&entry), -1);

    cbmfs_unmount();
}

/* --- File reading --- */

TEST(read_small_prg) {
    format_disk("READTEST", "AB");

    uint8_t prg[] = {0x01, 0x08, 'H', 'E', 'L', 'L', 'O'};
    ASSERT(add_file("HELLO", prg, sizeof(prg)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("HELLO", &handle), 0);
    ASSERT(handle.active);
    ASSERT(!handle.eof);

    for (int i = 0; i < (int)sizeof(prg); i++) {
        int val = cbmfs_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, prg[i]);
    }

    ASSERT(handle.eof);
    ASSERT_EQ(cbmfs_file_read_byte(&handle), -1);

    cbmfs_file_close(&handle);
    cbmfs_unmount();
}

TEST(read_multiblock_prg) {
    format_disk("MULTI", "AB");

    uint8_t prg[700];
    for (int i = 0; i < 700; i++) prg[i] = (uint8_t)(i & 0xFF);
    prg[0] = 0x01; prg[1] = 0x08;
    ASSERT(add_file("BIGFILE", prg, sizeof(prg)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("BIGFILE", &handle), 0);

    for (int i = 0; i < 700; i++) {
        int val = cbmfs_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, prg[i]);
    }

    ASSERT(handle.eof);
    cbmfs_file_close(&handle);
    cbmfs_unmount();
}

TEST(read_file_not_found) {
    format_disk("TEST", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("NOPE", &handle), -1);

    cbmfs_unmount();
}

TEST(read_exact_block_boundary) {
    format_disk("BOUNDARY", "AB");

    /* Exactly 254 bytes = one full data block */
    uint8_t prg[254];
    for (int i = 0; i < 254; i++) prg[i] = (uint8_t)i;
    ASSERT(add_file("EXACT254", prg, sizeof(prg)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("EXACT254", &handle), 0);

    for (int i = 0; i < 254; i++) {
        int val = cbmfs_file_read_byte(&handle);
        ASSERT(val >= 0);
        ASSERT_EQ(val, (uint8_t)i);
    }

    ASSERT(handle.eof);
    cbmfs_file_close(&handle);
    cbmfs_unmount();
}

/* --- File writing --- */

TEST(write_small_file) {
    format_disk("WRITE", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_create("NEWFILE", &handle), 0);
    ASSERT(handle.active);
    ASSERT(handle.write_mode);

    uint8_t data[] = {0x01, 0x08, 0xDE, 0xAD, 0xBE, 0xEF};
    for (int i = 0; i < (int)sizeof(data); i++) {
        ASSERT_EQ(cbmfs_file_write_byte(&handle, data[i]), 0);
    }

    cbmfs_file_close(&handle);

    /* Read it back */
    cbmfs_file_handle_t rh;
    ASSERT_EQ(cbmfs_file_open("NEWFILE", &rh), 0);
    for (int i = 0; i < (int)sizeof(data); i++) {
        int val = cbmfs_file_read_byte(&rh);
        ASSERT_EQ(val, data[i]);
    }
    ASSERT(rh.eof);
    cbmfs_file_close(&rh);

    cbmfs_unmount();
}

TEST(write_multiblock_file) {
    format_disk("BIGWRITE", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_create("BIGDATA", &handle), 0);

    uint8_t data[600];
    for (int i = 0; i < 600; i++) data[i] = (uint8_t)(i & 0xFF);

    for (int i = 0; i < 600; i++) {
        ASSERT_EQ(cbmfs_file_write_byte(&handle, data[i]), 0);
    }

    cbmfs_file_close(&handle);

    /* Read back */
    cbmfs_file_handle_t rh;
    ASSERT_EQ(cbmfs_file_open("BIGDATA", &rh), 0);
    for (int i = 0; i < 600; i++) {
        int val = cbmfs_file_read_byte(&rh);
        ASSERT(val >= 0);
        ASSERT_EQ(val, data[i]);
    }
    ASSERT(rh.eof);
    cbmfs_file_close(&rh);

    cbmfs_unmount();
}

/* --- File deletion --- */

TEST(delete_file) {
    format_disk("DELETE", "AB");

    uint8_t prg[] = {0x01, 0x08, 0x42};
    ASSERT(add_file("DELME", prg, sizeof(prg)));
    ASSERT(add_file("KEEPME", prg, sizeof(prg)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    /* Both should exist */
    cbmfs_dir_entry_t entry;
    ASSERT_EQ(cbmfs_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "DELME");
    ASSERT_EQ(cbmfs_dir_next(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "KEEPME");

    /* Delete DELME */
    ASSERT_EQ(cbmfs_file_delete("DELME"), 0);

    /* Only KEEPME should remain */
    ASSERT_EQ(cbmfs_dir_first(&entry), 0);
    ASSERT_STR_EQ(entry.filename, "KEEPME");
    ASSERT_EQ(cbmfs_dir_next(&entry), -1);

    /* Can't open deleted file */
    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("DELME", &handle), -1);

    cbmfs_unmount();
}

TEST(delete_not_found) {
    format_disk("TEST", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    ASSERT_EQ(cbmfs_file_delete("NOPE"), -1);

    cbmfs_unmount();
}

/* --- BAM integrity --- */

TEST(bam_updated_after_write) {
    format_disk("BAM TEST", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    uint16_t initial_free = cbmfs_free_blocks();
    ASSERT_EQ(initial_free, 3160);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_create("SMALL", &handle), 0);
    cbmfs_file_write_byte(&handle, 0x42);
    cbmfs_file_close(&handle);

    uint16_t new_free = cbmfs_free_blocks();
    ASSERT_EQ(new_free, 3159);

    cbmfs_unmount();
}

TEST(bam_restored_after_delete) {
    format_disk("BAM DEL", "AB");

    uint8_t prg[] = {0x01, 0x08, 0xAA};
    ASSERT(add_file("DELTEST", prg, sizeof(prg)));

    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    /* Should have 3159 free (one block used) */
    ASSERT_EQ(cbmfs_free_blocks(), 3159);

    ASSERT_EQ(cbmfs_file_delete("DELTEST"), 0);

    /* Should be back to 3160 */
    ASSERT_EQ(cbmfs_free_blocks(), 3160);

    cbmfs_unmount();
}

/* --- Disk name --- */

TEST(disk_name_long) {
    format_disk("0123456789ABCDEF", "XY");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    char name[17];
    cbmfs_get_disk_name(name);
    ASSERT_STR_EQ(name, "0123456789ABCDEF");

    cbmfs_unmount();
}

TEST(disk_name_not_mounted) {
    cbmfs_unmount();
    char name[17] = "GARBAGE";
    cbmfs_get_disk_name(name);
    ASSERT_EQ(name[0], 0);
}

/* --- Edge cases --- */

TEST(operations_when_not_mounted) {
    cbmfs_unmount();

    cbmfs_dir_entry_t entry;
    ASSERT_EQ(cbmfs_dir_first(&entry), -1);

    cbmfs_file_handle_t handle;
    ASSERT_EQ(cbmfs_file_open("TEST", &handle), -1);
    ASSERT_EQ(cbmfs_file_create("TEST", &handle), -1);
    ASSERT_EQ(cbmfs_file_delete("TEST"), -1);

    ASSERT(!cbmfs_is_mounted());
    ASSERT_EQ(cbmfs_free_blocks(), 0);
}

TEST(write_read_roundtrip) {
    format_disk("ROUNDTRIP", "AB");
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    /* Write */
    cbmfs_file_handle_t wh;
    ASSERT_EQ(cbmfs_file_create("MYDATA", &wh), 0);
    uint8_t test_data[100];
    for (int i = 0; i < 100; i++) test_data[i] = (uint8_t)(i * 3);
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(cbmfs_file_write_byte(&wh, test_data[i]), 0);
    }
    cbmfs_file_close(&wh);
    cbmfs_unmount();

    /* Remount and read */
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_file_handle_t rh;
    ASSERT_EQ(cbmfs_file_open("MYDATA", &rh), 0);
    for (int i = 0; i < 100; i++) {
        int val = cbmfs_file_read_byte(&rh);
        ASSERT_EQ(val, test_data[i]);
    }
    ASSERT(rh.eof);
    cbmfs_file_close(&rh);

    cbmfs_unmount();
}

TEST(multiple_files_write_and_read) {
    format_disk("MULTI WR", "AB");

    for (int f = 0; f < 3; f++) {
        ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

        char name[17];
        snprintf(name, sizeof(name), "FILE%d", f);

        cbmfs_file_handle_t wh;
        ASSERT_EQ(cbmfs_file_create(name, &wh), 0);
        for (int i = 0; i < 50; i++) {
            cbmfs_file_write_byte(&wh, (uint8_t)(f * 50 + i));
        }
        cbmfs_file_close(&wh);
        cbmfs_unmount();
    }

    /* Verify */
    ASSERT_EQ(cbmfs_mount(&mock_io), CBMFS_OK);

    cbmfs_dir_entry_t entry;
    int count = 0;
    int rc = cbmfs_dir_first(&entry);
    while (rc == 0) {
        count++;
        rc = cbmfs_dir_next(&entry);
    }
    ASSERT_EQ(count, 3);

    for (int f = 0; f < 3; f++) {
        char name[17];
        snprintf(name, sizeof(name), "FILE%d", f);

        cbmfs_file_handle_t rh;
        ASSERT_EQ(cbmfs_file_open(name, &rh), 0);
        for (int i = 0; i < 50; i++) {
            int val = cbmfs_file_read_byte(&rh);
            ASSERT_EQ(val, (uint8_t)(f * 50 + i));
        }
        cbmfs_file_close(&rh);
    }

    cbmfs_unmount();
}

/* --- Format detection --- */

TEST(format_detect_cbmfs) {
    format_disk("DETECT ME", "AB");

    /* Verify the header is valid CBMFS */
    int32_t off = logical_sector_offset(40, 0);
    ASSERT_EQ(disk_image[off + CBMFS_HDR_DOS_VERSION], 0x44);
    ASSERT_EQ(disk_image[off + CBMFS_HDR_DOS_TYPE], '3');
    ASSERT_EQ(disk_image[off + CBMFS_HDR_DOS_TYPE + 1], 'D');
}

/* ---- Main ---- */

int main(void) {
    printf("=== CBMFS (1581) Unit Tests ===\n\n");

    printf("Constants:\n");
    RUN(image_constants);

    printf("\nFormat:\n");
    RUN(format_creates_valid_disk);
    RUN(format_initializes_bam);

    printf("\nMount/Unmount:\n");
    RUN(mount_formatted_disk);
    RUN(mount_invalid_disk);
    RUN(free_blocks_after_format);

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

    printf("\nFormat detection:\n");
    RUN(format_detect_cbmfs);

    printf("\n========================================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n========================================\n");

    if (disk_image) free(disk_image);

    return tests_failed > 0 ? 1 : 0;
}
