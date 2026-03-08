/*
 * Unit tests for Nano FAT12 implementation.
 *
 * Mocks: SRAM (128KB array), disk I/O (1.44MB array).
 * Tests the platform-independent FAT12 logic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "fat12.h"

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

/* ---- Mock SRAM (128KB array) ---- */

static uint8_t mock_sram[131072];  /* 128KB */

void sram_read(uint32_t addr, uint8_t *buf, uint16_t len) {
    if (addr + len <= sizeof(mock_sram)) {
        memcpy(buf, &mock_sram[addr], len);
    }
}

void sram_write(uint32_t addr, const uint8_t *buf, uint16_t len) {
    if (addr + len <= sizeof(mock_sram)) {
        memcpy(&mock_sram[addr], buf, len);
    }
}

/* ---- Mock disk I/O (1.44MB array) ---- */

#define MOCK_DISK_SIZE (2880 * 512)
static uint8_t *mock_disk;

int disk_read_sector(uint16_t lba, uint8_t *buf) {
    if (lba >= 2880) return -1;
    memcpy(buf, &mock_disk[(uint32_t)lba * 512], 512);
    return 0;
}

int disk_write_sector(uint16_t lba, const uint8_t *buf) {
    if (lba >= 2880) return -1;
    memcpy(&mock_disk[(uint32_t)lba * 512], buf, 512);
    return 0;
}

/* ---- Test helpers ---- */

static void reset_mocks(void) {
    memset(mock_sram, 0, sizeof(mock_sram));
    memset(mock_disk, 0, MOCK_DISK_SIZE);
    fat12_unmount();
}

static int format_and_mount(void) {
    return fat12_format("TEST DISK  ");
}

/* Create a file with given data, returns FAT12_OK on success */
static int create_file(const char *name, const char *ext,
                       const uint8_t *data, uint16_t len) {
    fat12_file_t file;
    int rc = fat12_create(name, ext, &file);
    if (rc != FAT12_OK) return rc;

    if (len > 0) {
        rc = fat12_write(&file, data, len);
        if (rc < 0) return rc;
    }

    return fat12_close(&file);
}

/* ---- Tests ---- */

/* --- Format & Mount --- */

TEST(format_creates_valid_disk) {
    reset_mocks();
    ASSERT_EQ(fat12_format("64KORPPU   "), FAT12_OK);

    /* Verify boot sector */
    fat12_bpb_t *bpb = (fat12_bpb_t *)mock_disk;
    ASSERT_EQ(bpb->bytes_per_sector, 512);
    ASSERT_EQ(bpb->sectors_per_cluster, 1);
    ASSERT_EQ(bpb->num_fats, 2);
    ASSERT_EQ(bpb->root_entries, 224);
    ASSERT_EQ(bpb->total_sectors, 2880);
    ASSERT_EQ(bpb->media_type, 0xF0);
    ASSERT_EQ(bpb->sectors_per_fat, 9);
    ASSERT_EQ(mock_disk[510], 0x55);
    ASSERT_EQ(mock_disk[511], 0xAA);
}

TEST(format_initializes_fat) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* FAT entries 0,1 should be reserved */
    uint8_t fat_start[3];
    memcpy(fat_start, &mock_disk[FAT12_FAT1_START * 512], 3);
    ASSERT_EQ(fat_start[0], 0xF0);
    ASSERT_EQ(fat_start[1], 0xFF);
    ASSERT_EQ(fat_start[2], 0xFF);

    /* FAT #2 should be identical */
    uint8_t fat2_start[3];
    memcpy(fat2_start, &mock_disk[FAT12_FAT2_START * 512], 3);
    ASSERT_EQ(fat2_start[0], 0xF0);
    ASSERT_EQ(fat2_start[1], 0xFF);
    ASSERT_EQ(fat2_start[2], 0xFF);
}

TEST(mount_unformatted_disk_fails) {
    reset_mocks();
    ASSERT_EQ(fat12_mount(), FAT12_ERR_NOT_FAT12);
}

TEST(mount_formatted_disk) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);
    fat12_unmount();
    ASSERT_EQ(fat12_mount(), FAT12_OK);
}

/* --- Free space --- */

TEST(free_space_after_format) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint32_t free = fat12_free_space();
    /* 2847 clusters * 512 bytes = 1,457,664 bytes */
    ASSERT_EQ(free, (uint32_t)FAT12_TOTAL_CLUSTERS * 512);
}

/* --- FAT entry operations --- */

TEST(fat_entry_read_write) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Write some FAT entries */
    fat12_write_fat_entry(2, 0x003);
    fat12_write_fat_entry(3, 0xFFF);
    fat12_write_fat_entry(10, 0x00B);
    fat12_write_fat_entry(11, 0xFFF);

    /* Read them back */
    ASSERT_EQ(fat12_read_fat_entry(2), 0x003);
    ASSERT_EQ(fat12_read_fat_entry(3), 0xFFF);
    ASSERT_EQ(fat12_read_fat_entry(10), 0x00B);
    ASSERT_EQ(fat12_read_fat_entry(11), 0xFFF);

    /* Unmodified entries should still be free */
    ASSERT_EQ(fat12_read_fat_entry(4), 0x000);
    ASSERT_EQ(fat12_read_fat_entry(100), 0x000);
}

TEST(fat_entry_even_odd_clusters) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Even cluster */
    fat12_write_fat_entry(4, 0x123);
    ASSERT_EQ(fat12_read_fat_entry(4), 0x123);

    /* Odd cluster */
    fat12_write_fat_entry(5, 0x456);
    ASSERT_EQ(fat12_read_fat_entry(5), 0x456);

    /* Verify they don't interfere */
    ASSERT_EQ(fat12_read_fat_entry(4), 0x123);
    ASSERT_EQ(fat12_read_fat_entry(5), 0x456);
}

/* --- File creation and writing --- */

TEST(create_and_write_small_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "Hello, C64!";
    ASSERT_EQ(create_file("HELLO   ", "PRG", data, sizeof(data) - 1), FAT12_OK);

    /* Verify file exists */
    fat12_dirent_t entry;
    ASSERT_EQ(fat12_find_file("HELLO   ", "PRG", &entry), FAT12_OK);
    ASSERT_EQ(entry.file_size, 11);
    ASSERT(entry.cluster_lo >= 2);
}

TEST(create_and_read_small_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "Hello, C64!";
    ASSERT_EQ(create_file("HELLO   ", "PRG", data, sizeof(data) - 1), FAT12_OK);

    /* Read it back */
    fat12_file_t file;
    ASSERT_EQ(fat12_open_read("HELLO   ", "PRG", &file), FAT12_OK);

    uint8_t buf[32];
    int n = fat12_read(&file, buf, sizeof(buf));
    ASSERT_EQ(n, 11);
    ASSERT(memcmp(buf, "Hello, C64!", 11) == 0);
}

TEST(write_multiblock_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Write 1024 bytes (2 clusters) */
    uint8_t data[1024];
    for (int i = 0; i < 1024; i++) data[i] = (uint8_t)(i & 0xFF);

    ASSERT_EQ(create_file("BIG     ", "PRG", data, 1024), FAT12_OK);

    /* Read it back */
    fat12_file_t file;
    ASSERT_EQ(fat12_open_read("BIG     ", "PRG", &file), FAT12_OK);
    ASSERT_EQ(file.file_size, 1024);

    uint8_t buf[1024];
    int n = fat12_read(&file, buf, 1024);
    ASSERT_EQ(n, 1024);
    for (int i = 0; i < 1024; i++) {
        ASSERT_EQ(buf[i], (uint8_t)(i & 0xFF));
    }
}

TEST(write_exact_sector_boundary) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Write exactly 512 bytes (1 cluster) */
    uint8_t data[512];
    memset(data, 0xAA, 512);

    ASSERT_EQ(create_file("EXACT   ", "PRG", data, 512), FAT12_OK);

    /* Read it back */
    fat12_file_t file;
    ASSERT_EQ(fat12_open_read("EXACT   ", "PRG", &file), FAT12_OK);

    uint8_t buf[512];
    int n = fat12_read(&file, buf, 512);
    ASSERT_EQ(n, 512);
    for (int i = 0; i < 512; i++) {
        ASSERT_EQ(buf[i], 0xAA);
    }
}

/* --- File deletion --- */

TEST(delete_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "test";
    ASSERT_EQ(create_file("DELME   ", "PRG", data, 4), FAT12_OK);

    /* Verify it exists */
    fat12_dirent_t entry;
    ASSERT_EQ(fat12_find_file("DELME   ", "PRG", &entry), FAT12_OK);

    /* Delete it */
    ASSERT_EQ(fat12_delete("DELME   ", "PRG"), FAT12_OK);

    /* Verify it's gone */
    ASSERT_EQ(fat12_find_file("DELME   ", "PRG", &entry), FAT12_ERR_NOT_FOUND);
}

TEST(delete_frees_clusters) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint32_t free_before = fat12_free_space();

    uint8_t data[1024];
    memset(data, 0, 1024);
    ASSERT_EQ(create_file("TEMP    ", "PRG", data, 1024), FAT12_OK);

    /* Free space should decrease by 2 clusters */
    uint32_t free_after_write = fat12_free_space();
    ASSERT_EQ(free_before - free_after_write, 2 * 512);

    /* Delete and verify free space is restored */
    ASSERT_EQ(fat12_delete("TEMP    ", "PRG"), FAT12_OK);
    uint32_t free_after_delete = fat12_free_space();
    ASSERT_EQ(free_after_delete, free_before);
}

TEST(delete_not_found) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);
    ASSERT_EQ(fat12_delete("NOFILE  ", "PRG"), FAT12_ERR_NOT_FOUND);
}

/* --- Rename --- */

TEST(rename_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "test";
    ASSERT_EQ(create_file("OLD     ", "PRG", data, 4), FAT12_OK);

    ASSERT_EQ(fat12_rename("OLD     ", "PRG", "NEW     ", "PRG"), FAT12_OK);

    /* Old name should not exist */
    fat12_dirent_t entry;
    ASSERT_EQ(fat12_find_file("OLD     ", "PRG", &entry), FAT12_ERR_NOT_FOUND);

    /* New name should exist */
    ASSERT_EQ(fat12_find_file("NEW     ", "PRG", &entry), FAT12_OK);
    ASSERT_EQ(entry.file_size, 4);
}

TEST(rename_to_existing_fails) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "a";
    ASSERT_EQ(create_file("FILE1   ", "PRG", data, 1), FAT12_OK);
    ASSERT_EQ(create_file("FILE2   ", "PRG", data, 1), FAT12_OK);

    ASSERT_EQ(fat12_rename("FILE1   ", "PRG", "FILE2   ", "PRG"), FAT12_ERR_EXISTS);
}

/* --- Directory listing --- */

TEST(readdir_empty_disk) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint16_t index = 0;
    fat12_dirent_t entry;

    /* Should skip volume label, find no files */
    int rc = fat12_readdir(&index, &entry);
    if (rc == FAT12_OK) {
        /* If volume label is returned, it should have VOLUME_ID attr */
        ASSERT(entry.attr & FAT12_ATTR_VOLUME_ID);
        /* No more entries */
        rc = fat12_readdir(&index, &entry);
    }
    /* Eventually we get NOT_FOUND */
}

TEST(readdir_with_files) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data[] = "x";
    ASSERT_EQ(create_file("FILE1   ", "PRG", data, 1), FAT12_OK);
    ASSERT_EQ(create_file("FILE2   ", "SEQ", data, 1), FAT12_OK);

    uint16_t index = 0;
    fat12_dirent_t entry;
    int count = 0;
    bool found_file1 = false, found_file2 = false;

    while (fat12_readdir(&index, &entry) == FAT12_OK) {
        if (entry.attr & FAT12_ATTR_VOLUME_ID) continue;
        count++;
        if (memcmp(entry.name, "FILE1   ", 8) == 0) found_file1 = true;
        if (memcmp(entry.name, "FILE2   ", 8) == 0) found_file2 = true;
    }

    ASSERT_EQ(count, 2);
    ASSERT(found_file1);
    ASSERT(found_file2);
}

/* --- Filename parsing --- */

TEST(parse_filename_with_extension) {
    char name[9] = {0}, ext[4] = {0};
    fat12_parse_filename("HELLO.PRG", name, ext);
    ASSERT(memcmp(name, "HELLO   ", 8) == 0);
    ASSERT(memcmp(ext, "PRG", 3) == 0);
}

TEST(parse_filename_without_extension) {
    char name[9] = {0}, ext[4] = {0};
    fat12_parse_filename("HELLO", name, ext);
    ASSERT(memcmp(name, "HELLO   ", 8) == 0);
    ASSERT(memcmp(ext, "PRG", 3) == 0);  /* Default to PRG */
}

TEST(parse_filename_lowercase) {
    char name[9] = {0}, ext[4] = {0};
    fat12_parse_filename("hello.seq", name, ext);
    ASSERT(memcmp(name, "HELLO   ", 8) == 0);
    ASSERT(memcmp(ext, "SEQ", 3) == 0);
}

/* --- Edge cases --- */

TEST(operations_when_not_mounted) {
    reset_mocks();
    /* Don't mount - operations should fail gracefully */

    fat12_dirent_t entry;
    ASSERT_EQ(fat12_find_file("X       ", "PRG", &entry), FAT12_ERR_NOT_MOUNT);
    ASSERT_EQ(fat12_free_space(), 0);
    ASSERT_EQ(fat12_read_fat_entry(2), 0);

    uint16_t index = 0;
    ASSERT_EQ(fat12_readdir(&index, &entry), FAT12_ERR_NOT_MOUNT);
}

TEST(write_then_read_roundtrip) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Write a file with known pattern */
    uint8_t pattern[768];
    for (int i = 0; i < 768; i++) pattern[i] = (uint8_t)((i * 7 + 13) & 0xFF);

    ASSERT_EQ(create_file("PATTERN ", "DAT", pattern, 768), FAT12_OK);

    /* Unmount and remount */
    fat12_unmount();
    ASSERT_EQ(fat12_mount(), FAT12_OK);

    /* Read back and verify */
    fat12_file_t file;
    ASSERT_EQ(fat12_open_read("PATTERN ", "DAT", &file), FAT12_OK);
    ASSERT_EQ(file.file_size, 768);

    uint8_t buf[768];
    int n = fat12_read(&file, buf, 768);
    ASSERT_EQ(n, 768);
    for (int i = 0; i < 768; i++) {
        ASSERT_EQ(buf[i], pattern[i]);
    }
}

TEST(multiple_files) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Create 5 files with different sizes */
    for (int f = 0; f < 5; f++) {
        char name[9];
        memset(name, ' ', 8);
        name[0] = 'A' + f;
        name[8] = '\0';

        uint16_t size = (f + 1) * 100;
        uint8_t data[500];
        memset(data, 'A' + f, size);

        ASSERT_EQ(create_file(name, "PRG", data, size), FAT12_OK);
    }

    /* Read each back and verify */
    for (int f = 0; f < 5; f++) {
        char name[9];
        memset(name, ' ', 8);
        name[0] = 'A' + f;

        fat12_file_t file;
        ASSERT_EQ(fat12_open_read(name, "PRG", &file), FAT12_OK);

        uint16_t expected_size = (f + 1) * 100;
        ASSERT_EQ(file.file_size, expected_size);

        uint8_t buf[500];
        int n = fat12_read(&file, buf, expected_size);
        ASSERT_EQ(n, expected_size);

        for (int i = 0; i < expected_size; i++) {
            ASSERT_EQ(buf[i], 'A' + f);
        }
    }
}

TEST(overwrite_existing_file) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    uint8_t data1[] = "original content";
    ASSERT_EQ(create_file("FILE    ", "PRG", data1, 16), FAT12_OK);

    /* Overwrite with different content */
    uint8_t data2[] = "new";
    ASSERT_EQ(create_file("FILE    ", "PRG", data2, 3), FAT12_OK);

    /* Read back - should get new content */
    fat12_file_t file;
    ASSERT_EQ(fat12_open_read("FILE    ", "PRG", &file), FAT12_OK);
    ASSERT_EQ(file.file_size, 3);

    uint8_t buf[32];
    int n = fat12_read(&file, buf, 32);
    ASSERT_EQ(n, 3);
    ASSERT(memcmp(buf, "new", 3) == 0);
}

TEST(fat_flush_writes_both_copies) {
    reset_mocks();
    ASSERT_EQ(format_and_mount(), FAT12_OK);

    /* Create a file to dirty the FAT */
    uint8_t data[] = "x";
    ASSERT_EQ(create_file("TEST    ", "PRG", data, 1), FAT12_OK);

    /* Verify both FAT copies are identical */
    for (int s = 0; s < FAT12_SECTORS_PER_FAT; s++) {
        uint8_t fat1[512], fat2[512];
        memcpy(fat1, &mock_disk[(FAT12_FAT1_START + s) * 512], 512);
        memcpy(fat2, &mock_disk[(FAT12_FAT2_START + s) * 512], 512);
        ASSERT(memcmp(fat1, fat2, 512) == 0);
    }
}

/* ---- Main ---- */

int main(void) {
    /* Allocate mock disk on heap (1.44MB is too large for stack) */
    mock_disk = (uint8_t *)malloc(MOCK_DISK_SIZE);
    if (!mock_disk) {
        fprintf(stderr, "Failed to allocate mock disk\n");
        return 1;
    }

    printf("=== Nano FAT12 Unit Tests ===\n\n");

    printf("Format & Mount:\n");
    RUN(format_creates_valid_disk);
    RUN(format_initializes_fat);
    RUN(mount_unformatted_disk_fails);
    RUN(mount_formatted_disk);

    printf("\nFree space:\n");
    RUN(free_space_after_format);

    printf("\nFAT entries:\n");
    RUN(fat_entry_read_write);
    RUN(fat_entry_even_odd_clusters);

    printf("\nFile creation & writing:\n");
    RUN(create_and_write_small_file);
    RUN(create_and_read_small_file);
    RUN(write_multiblock_file);
    RUN(write_exact_sector_boundary);

    printf("\nFile deletion:\n");
    RUN(delete_file);
    RUN(delete_frees_clusters);
    RUN(delete_not_found);

    printf("\nRename:\n");
    RUN(rename_file);
    RUN(rename_to_existing_fails);

    printf("\nDirectory listing:\n");
    RUN(readdir_empty_disk);
    RUN(readdir_with_files);

    printf("\nFilename parsing:\n");
    RUN(parse_filename_with_extension);
    RUN(parse_filename_without_extension);
    RUN(parse_filename_lowercase);

    printf("\nEdge cases:\n");
    RUN(operations_when_not_mounted);
    RUN(write_then_read_roundtrip);
    RUN(multiple_files);
    RUN(overwrite_existing_file);
    RUN(fat_flush_writes_both_copies);

    printf("\n========================================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n========================================\n");

    free(mock_disk);
    return tests_failed > 0 ? 1 : 0;
}
