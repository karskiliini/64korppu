/*
 * Unit tests for disk format detection and geometry.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disk_format.h"
#include "fat12.h"
#include "cbmfs.h"

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

/* ---- Mock sector I/O ---- */

/* Large enough for both HD (2880 sectors) and DD (1600 sectors) */
#define MOCK_DISK_SIZE  (2880 * 512)
static uint8_t mock_disk[MOCK_DISK_SIZE];

static int mock_read_sector(uint16_t lba, uint8_t *buf) {
    if (lba * 512 + 512 > MOCK_DISK_SIZE) return -1;
    memcpy(buf, &mock_disk[lba * 512], 512);
    return 0;
}

/*
 * Create a minimal FAT12 1.44MB boot sector.
 */
static void create_fat12_hd(void) {
    memset(mock_disk, 0, MOCK_DISK_SIZE);

    fat12_bpb_t *bpb = (fat12_bpb_t *)mock_disk;
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem_name, "64KORPPU", 8);
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 1;
    bpb->reserved_sectors = 1;
    bpb->num_fats = 2;
    bpb->root_entries = 224;
    bpb->total_sectors = 2880;
    bpb->media_type = 0xF0;
    bpb->sectors_per_fat = 9;
    bpb->sectors_per_track = 18;
    bpb->num_heads = 2;
    bpb->boot_sig = 0x29;
    memcpy(bpb->volume_label, "64KORPPU   ", 11);
    memcpy(bpb->fs_type, "FAT12   ", 8);
    mock_disk[510] = 0x55;
    mock_disk[511] = 0xAA;
}

/*
 * Create a minimal FAT12 720KB boot sector.
 */
static void create_fat12_dd(void) {
    memset(mock_disk, 0, MOCK_DISK_SIZE);

    fat12_bpb_t *bpb = (fat12_bpb_t *)mock_disk;
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem_name, "MSDOS5.0", 8);
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 2;
    bpb->reserved_sectors = 1;
    bpb->num_fats = 2;
    bpb->root_entries = 112;
    bpb->total_sectors = 1440;
    bpb->media_type = 0xF9;
    bpb->sectors_per_fat = 3;
    bpb->sectors_per_track = 9;
    bpb->num_heads = 2;
    bpb->boot_sig = 0x29;
    memcpy(bpb->volume_label, "NO NAME    ", 11);
    memcpy(bpb->fs_type, "FAT12   ", 8);
    mock_disk[510] = 0x55;
    mock_disk[511] = 0xAA;
}

/*
 * Create a CBMFS 1581 header at the correct LBA for DD geometry.
 *
 * Logical track 40, sector 0 → LBA 780 (first 256 bytes of physical sector).
 */
static void create_cbmfs_dd(void) {
    memset(mock_disk, 0, MOCK_DISK_SIZE);

    /* Physical LBA for logical T40/S0 = (39*2+0)*10 + 0 = 780 */
    uint16_t lba = 780;
    uint8_t *sector = &mock_disk[lba * 512];

    /* Header in first 256 bytes */
    sector[CBMFS_HDR_DIR_TRACK] = 40;
    sector[CBMFS_HDR_DIR_SECTOR] = 3;
    sector[CBMFS_HDR_DOS_VERSION] = 0x44;  /* 'D' */

    /* Disk name */
    memset(&sector[CBMFS_HDR_DISK_NAME], 0xA0, 16);
    memcpy(&sector[CBMFS_HDR_DISK_NAME], "TEST DISK", 9);

    /* Disk ID */
    sector[CBMFS_HDR_DISK_ID] = '6';
    sector[CBMFS_HDR_DISK_ID + 1] = '4';

    /* DOS type "3D" */
    sector[CBMFS_HDR_DOS_TYPE] = '3';
    sector[CBMFS_HDR_DOS_TYPE + 1] = 'D';
}

/* ---- Tests ---- */

/* --- Geometry constants --- */

TEST(geometry_hd) {
    const disk_geometry_t *g = &DISK_GEOM_HD;
    ASSERT_EQ(g->tracks, 80);
    ASSERT_EQ(g->sides, 2);
    ASSERT_EQ(g->sectors_per_track, 18);
    ASSERT_EQ(g->sector_size, 512);
    ASSERT_EQ(g->total_sectors, 2880);
    ASSERT_EQ(g->data_rate_kbps, 500);
}

TEST(geometry_dd) {
    const disk_geometry_t *g = &DISK_GEOM_DD;
    ASSERT_EQ(g->tracks, 80);
    ASSERT_EQ(g->sides, 2);
    ASSERT_EQ(g->sectors_per_track, 10);
    ASSERT_EQ(g->sector_size, 512);
    ASSERT_EQ(g->total_sectors, 1600);
    ASSERT_EQ(g->data_rate_kbps, 250);
}

TEST(geometry_for_density_hd) {
    const disk_geometry_t *g = geometry_for_density(DISK_DENSITY_HD);
    ASSERT_EQ(g->sectors_per_track, 18);
    ASSERT_EQ(g->data_rate_kbps, 500);
}

TEST(geometry_for_density_dd) {
    const disk_geometry_t *g = geometry_for_density(DISK_DENSITY_DD);
    ASSERT_EQ(g->sectors_per_track, 10);
    ASSERT_EQ(g->data_rate_kbps, 250);
}

/* --- LBA/CHS conversion --- */

TEST(lba_to_chs_hd) {
    const disk_geometry_t *g = &DISK_GEOM_HD;
    uint8_t track, side, sector;

    geom_lba_to_chs(g, 0, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 1);

    geom_lba_to_chs(g, 17, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 18);

    geom_lba_to_chs(g, 18, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 1);
    ASSERT_EQ(sector, 1);

    geom_lba_to_chs(g, 36, &track, &side, &sector);
    ASSERT_EQ(track, 1);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 1);
}

TEST(lba_to_chs_dd) {
    const disk_geometry_t *g = &DISK_GEOM_DD;
    uint8_t track, side, sector;

    geom_lba_to_chs(g, 0, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 1);

    geom_lba_to_chs(g, 9, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 10);

    geom_lba_to_chs(g, 10, &track, &side, &sector);
    ASSERT_EQ(track, 0);
    ASSERT_EQ(side, 1);
    ASSERT_EQ(sector, 1);

    geom_lba_to_chs(g, 20, &track, &side, &sector);
    ASSERT_EQ(track, 1);
    ASSERT_EQ(side, 0);
    ASSERT_EQ(sector, 1);
}

TEST(chs_to_lba_roundtrip) {
    const disk_geometry_t *g = &DISK_GEOM_HD;

    for (uint16_t lba = 0; lba < 2880; lba++) {
        uint8_t t, s, sec;
        geom_lba_to_chs(g, lba, &t, &s, &sec);
        uint16_t result = geom_chs_to_lba(g, t, s, sec);
        ASSERT_EQ(result, lba);
    }
}

/* --- Format detection --- */

TEST(detect_fat12_hd) {
    create_fat12_hd();
    disk_format_t fmt = format_detect(DISK_DENSITY_HD, mock_read_sector);
    ASSERT_EQ(fmt, DISK_FORMAT_FAT12_HD);
}

TEST(detect_fat12_dd) {
    create_fat12_dd();
    disk_format_t fmt = format_detect(DISK_DENSITY_DD, mock_read_sector);
    ASSERT_EQ(fmt, DISK_FORMAT_FAT12_DD);
}

TEST(detect_cbmfs_1581) {
    create_cbmfs_dd();
    disk_format_t fmt = format_detect(DISK_DENSITY_DD, mock_read_sector);
    ASSERT_EQ(fmt, DISK_FORMAT_CBMFS_1581);
}

TEST(detect_empty_disk) {
    memset(mock_disk, 0, MOCK_DISK_SIZE);
    disk_format_t fmt = format_detect(DISK_DENSITY_HD, mock_read_sector);
    ASSERT_EQ(fmt, DISK_FORMAT_NONE);
}

TEST(detect_cbmfs_preferred_over_fat12_dd) {
    /* If both CBMFS header and FAT12 boot sector are present on DD,
     * CBMFS should be detected first */
    create_cbmfs_dd();
    /* Also write a FAT12-720K boot sector */
    fat12_bpb_t *bpb = (fat12_bpb_t *)mock_disk;
    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 2;
    bpb->num_fats = 2;
    bpb->total_sectors = 1440;
    bpb->media_type = 0xF9;
    memcpy(bpb->fs_type, "FAT12   ", 8);
    mock_disk[510] = 0x55;
    mock_disk[511] = 0xAA;

    disk_format_t fmt = format_detect(DISK_DENSITY_DD, mock_read_sector);
    ASSERT_EQ(fmt, DISK_FORMAT_CBMFS_1581);
}

/* --- Format ID strings --- */

TEST(format_id_strings) {
    ASSERT_STR_EQ(format_id_string(DISK_FORMAT_FAT12_HD), "FAT");
    ASSERT_STR_EQ(format_id_string(DISK_FORMAT_FAT12_DD), "72K");
    ASSERT_STR_EQ(format_id_string(DISK_FORMAT_CBMFS_1581), "3D");
    ASSERT_STR_EQ(format_id_string(DISK_FORMAT_NONE), "???");
}

/* --- Device number --- */

TEST(default_device_number) {
    uint8_t dev = device_number_from_jumpers();
    ASSERT_EQ(dev, 8);
}

/* ---- Main ---- */

int main(void) {
    printf("=== Format Detection & Geometry Tests ===\n\n");

    printf("Geometry:\n");
    RUN(geometry_hd);
    RUN(geometry_dd);
    RUN(geometry_for_density_hd);
    RUN(geometry_for_density_dd);

    printf("\nLBA/CHS conversion:\n");
    RUN(lba_to_chs_hd);
    RUN(lba_to_chs_dd);
    RUN(chs_to_lba_roundtrip);

    printf("\nFormat detection:\n");
    RUN(detect_fat12_hd);
    RUN(detect_fat12_dd);
    RUN(detect_cbmfs_1581);
    RUN(detect_empty_disk);
    RUN(detect_cbmfs_preferred_over_fat12_dd);

    printf("\nFormat ID strings:\n");
    RUN(format_id_strings);

    printf("\nDevice number:\n");
    RUN(default_device_number);

    printf("\n========================================\n");
    printf("Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
