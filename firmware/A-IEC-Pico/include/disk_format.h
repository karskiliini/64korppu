#ifndef DISK_FORMAT_H
#define DISK_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Disk geometry and format detection.
 *
 * Supports two physical densities:
 *   HD (1.44MB): 80 tracks, 2 sides, 18 sectors/track, 512B, 500 kbps
 *   DD (800KB):  80 tracks, 2 sides, 10 sectors/track, 512B, 250 kbps
 *
 * Filesystem formats:
 *   FAT12_HD:    Standard PC 1.44MB FAT12
 *   FAT12_DD:    Standard PC 720KB FAT12
 *   CBMFS_1581:  Commodore 1581 compatible CBMFS
 */

/* Runtime disk geometry (replaces compile-time constants) */
typedef struct {
    uint8_t  tracks;            /* Number of tracks (80) */
    uint8_t  sides;             /* Number of sides (2) */
    uint8_t  sectors_per_track; /* Physical sectors per track per side */
    uint16_t sector_size;       /* Physical sector size in bytes (512) */
    uint16_t total_sectors;     /* Total physical sectors */
    uint16_t data_rate_kbps;    /* Data rate: 500 (HD) or 250 (DD) */
} disk_geometry_t;

/* Predefined geometries */
extern const disk_geometry_t DISK_GEOM_HD;  /* 80x2x18, 500 kbps */
extern const disk_geometry_t DISK_GEOM_DD;  /* 80x2x10, 250 kbps */

/* Detected disk format */
typedef enum {
    DISK_FORMAT_NONE = 0,       /* No disk or unrecognized */
    DISK_FORMAT_FAT12_HD,       /* 1.44MB FAT12 */
    DISK_FORMAT_FAT12_DD,       /* 720KB FAT12 */
    DISK_FORMAT_CBMFS_1581,     /* 800KB Commodore 1581 CBMFS */
} disk_format_t;

/* Physical density from HD/DD sensor */
typedef enum {
    DISK_DENSITY_HD = 0,        /* HD hole present (pin LOW) */
    DISK_DENSITY_DD = 1,        /* No HD hole (pin HIGH) */
} disk_density_t;

/*
 * Device number configuration.
 *
 * Two jumpers select device 8-11 (like 1541):
 *   J1=open,  J2=open  → device 8
 *   J1=close, J2=open  → device 9
 *   J1=open,  J2=close → device 10
 *   J1=close, J2=close → device 11
 *
 * Future: EEPROM override bit allows software-configurable device number.
 *   If EEPROM byte 0 bit 7 is set, bits 0-4 specify device number (8-30).
 *   Command U0>N sets device number N, U0>J reverts to jumper mode.
 */
#define DEVICE_NUM_JUMPER1_PIN  19   /* GPIO for jumper 1 (directly on Pico) */
#define DEVICE_NUM_JUMPER2_PIN  20   /* GPIO for jumper 2 */
#define DEVICE_NUM_DEFAULT       8   /* Default when no jumpers */

/**
 * Read device number from jumper pins.
 * Jumpers active low (pulled up internally, jumper connects to GND).
 * @return Device number 8-11.
 */
uint8_t device_number_from_jumpers(void);

/**
 * Detect disk format by reading key sectors.
 *
 * For HD disks: reads boot sector (LBA 0) to check FAT12 BPB.
 * For DD disks: reads track 40 sector 0 to check CBMFS header,
 *               then falls back to FAT12-720K check.
 *
 * @param density  Physical density from HD/DD sensor.
 * @param read_sector  Function to read a physical sector by LBA.
 * @return Detected format, or DISK_FORMAT_NONE if unrecognized.
 */
disk_format_t format_detect(disk_density_t density,
                             int (*read_sector)(uint16_t lba, uint8_t *buf));

/**
 * Get human-readable format name for directory listing header.
 * @return Short format ID string (e.g. "FAT", "3D", "72K").
 */
const char *format_id_string(disk_format_t format);

/**
 * Get the geometry for a given density.
 */
const disk_geometry_t *geometry_for_density(disk_density_t density);

/**
 * Convert LBA to CHS using given geometry.
 */
static inline void geom_lba_to_chs(const disk_geometry_t *geom, uint16_t lba,
                                     uint8_t *track, uint8_t *side, uint8_t *sector) {
    uint16_t spt = geom->sectors_per_track;
    *track = lba / (geom->sides * spt);
    uint16_t temp = lba % (geom->sides * spt);
    *side = temp / spt;
    *sector = (temp % spt) + 1;  /* 1-based */
}

/**
 * Convert CHS to LBA using given geometry.
 */
static inline uint16_t geom_chs_to_lba(const disk_geometry_t *geom,
                                         uint8_t track, uint8_t side, uint8_t sector) {
    return (track * geom->sides + side) * geom->sectors_per_track + (sector - 1);
}

#endif /* DISK_FORMAT_H */
