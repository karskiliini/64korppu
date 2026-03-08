#include "disk_format.h"
#include "fat12.h"
#include "cbmfs.h"
#include <string.h>

/* Predefined geometries */
const disk_geometry_t DISK_GEOM_HD = {
    .tracks = 80,
    .sides = 2,
    .sectors_per_track = 18,
    .sector_size = 512,
    .total_sectors = 2880,
    .data_rate_kbps = 500,
};

const disk_geometry_t DISK_GEOM_DD = {
    .tracks = 80,
    .sides = 2,
    .sectors_per_track = 10,
    .sector_size = 512,
    .total_sectors = 1600,
    .data_rate_kbps = 250,
};

const disk_geometry_t *geometry_for_density(disk_density_t density) {
    return (density == DISK_DENSITY_HD) ? &DISK_GEOM_HD : &DISK_GEOM_DD;
}

const char *format_id_string(disk_format_t format) {
    switch (format) {
        case DISK_FORMAT_FAT12_HD:    return "FAT";
        case DISK_FORMAT_FAT12_DD:    return "72K";
        case DISK_FORMAT_CBMFS_1581:  return "3D";
        default:                      return "???";
    }
}

/*
 * Check if a sector contains a valid FAT12 BPB.
 */
static bool is_fat12_bpb(const uint8_t *sector, uint16_t expected_total) {
    fat12_bpb_t *bpb = (fat12_bpb_t *)sector;

    /* Check basic BPB sanity */
    if (bpb->bytes_per_sector != 512) return false;
    if (bpb->num_fats != 2) return false;
    if (bpb->sectors_per_cluster == 0) return false;

    /* Check total sectors */
    if (expected_total > 0 && bpb->total_sectors != expected_total) return false;

    /* Check media type */
    if (bpb->media_type != 0xF0 && bpb->media_type != 0xF9) return false;

    /* Check boot signature (optional but strong indicator) */
    if (sector[510] == 0x55 && sector[511] == 0xAA) return true;

    /* Without boot signature, require filesystem type string */
    if (memcmp(bpb->fs_type, "FAT12   ", 8) == 0) return true;

    return false;
}

/*
 * Check if track 40, sector 0 contains a 1581 CBMFS header.
 *
 * For DD disk with 10 sectors/track/side:
 * Logical track 40, sector 0 maps to:
 *   Physical track 39, side 0, physical sector 1 (first half of 512 bytes)
 * LBA = (39 * 2 + 0) * 10 + (1 - 1) = 780
 */
static bool check_cbmfs_header(int (*read_sector)(uint16_t lba, uint8_t *buf)) {
    uint8_t phys_buf[512];

    /* CBMFS logical track 40, sector 0
     * Physical: track 39, side 0, sector 1 → LBA 780 */
    uint16_t lba = (39 * 2 + 0) * 10 + 0;  /* = 780 */
    if (read_sector(lba, phys_buf) != 0) return false;

    /* Logical sector 0 is the first 256 bytes of the physical sector */
    uint8_t *header = phys_buf;

    /* Check DOS version byte at offset 2 */
    if (header[CBMFS_HDR_DOS_VERSION] != CBMFS_DOS_VERSION) return false;

    /* Check DOS type "3D" at offset 25 */
    if (header[CBMFS_HDR_DOS_TYPE] != '3' ||
        header[CBMFS_HDR_DOS_TYPE + 1] != 'D') return false;

    /* Check directory pointer is sane */
    if (header[CBMFS_HDR_DIR_TRACK] != CBMFS_DIR_TRACK ||
        header[CBMFS_HDR_DIR_SECTOR] != CBMFS_DIR_FIRST_SECTOR) return false;

    return true;
}

disk_format_t format_detect(disk_density_t density,
                             int (*read_sector)(uint16_t lba, uint8_t *buf)) {
    uint8_t buf[512];

    if (density == DISK_DENSITY_HD) {
        /* HD disk: check for FAT12 1.44MB */
        if (read_sector(0, buf) != 0) return DISK_FORMAT_NONE;
        if (is_fat12_bpb(buf, FAT12_TOTAL_SECTORS)) return DISK_FORMAT_FAT12_HD;
        return DISK_FORMAT_NONE;
    }

    /* DD disk: first check for CBMFS 1581 */
    if (check_cbmfs_header(read_sector)) return DISK_FORMAT_CBMFS_1581;

    /* Fall back to FAT12-720K check */
    if (read_sector(0, buf) != 0) return DISK_FORMAT_NONE;
    if (is_fat12_bpb(buf, 1440)) return DISK_FORMAT_FAT12_DD;

    return DISK_FORMAT_NONE;
}

#ifndef PICO_ON_DEVICE
/* Host build: provide stub for device_number_from_jumpers */
uint8_t device_number_from_jumpers(void) {
    return DEVICE_NUM_DEFAULT;
}
#endif
