#ifndef FLOPPY_CTRL_H
#define FLOPPY_CTRL_H

#include <stdint.h>
#include <stdbool.h>

/*
 * PC 3.5" HD floppy drive control via 34-pin Shugart interface.
 *
 * 1.44MB format: 80 tracks, 2 sides, 18 sectors/track, 512 bytes/sector
 */

/* GPIO pin assignments for floppy interface */
#define FLOPPY_PIN_DENSITY   6   /* /DENSITY - LOW=HD */
#define FLOPPY_PIN_MOTEA     7   /* /MOTEA - Motor enable A */
#define FLOPPY_PIN_DRVSEL    8   /* /DRVSB - Drive select B */
#define FLOPPY_PIN_MOTOR     9   /* /MOTOR - Motor on */
#define FLOPPY_PIN_DIR      10   /* /DIR - Step direction (LOW=inward) */
#define FLOPPY_PIN_STEP     11   /* /STEP - Step pulse */
#define FLOPPY_PIN_WDATA    12   /* /WDATA - Write data */
#define FLOPPY_PIN_WGATE    13   /* /WGATE - Write gate */
#define FLOPPY_PIN_TRK00    14   /* /TRK00 - Track 0 detect */
#define FLOPPY_PIN_WPT      15   /* /WPT - Write protect */
#define FLOPPY_PIN_RDATA    16   /* /RDATA - Read data */
#define FLOPPY_PIN_SIDE1    17   /* /SIDE1 - Head select (LOW=side 1) */
#define FLOPPY_PIN_DSKCHG   18   /* /DSKCHG - Disk change */

/* Floppy geometry for 1.44MB HD */
#define FLOPPY_TRACKS        80
#define FLOPPY_SIDES          2
#define FLOPPY_SECTORS       18
#define FLOPPY_SECTOR_SIZE  512
#define FLOPPY_TRACK_SIZE   (FLOPPY_SECTORS * FLOPPY_SECTOR_SIZE)
#define FLOPPY_TOTAL_SECTORS (FLOPPY_TRACKS * FLOPPY_SIDES * FLOPPY_SECTORS)

/* Data rate for HD: 500 kbit/s, cell time = 2us */
#define FLOPPY_DATA_RATE_KBPS  500
#define FLOPPY_CELL_TIME_NS   2000

/* Timing constants (microseconds) */
#define FLOPPY_STEP_PULSE_US      3    /* Minimum step pulse width */
#define FLOPPY_STEP_SETTLE_MS    15    /* Head settle time after seek */
#define FLOPPY_MOTOR_SPIN_MS    500    /* Motor spin-up time */
#define FLOPPY_STEP_RATE_MS       3    /* Time between steps */

/* Error codes */
#define FLOPPY_OK                 0
#define FLOPPY_ERR_NO_TRK0      -1    /* Track 0 not found during recalibrate */
#define FLOPPY_ERR_SEEK          -2    /* Seek failed */
#define FLOPPY_ERR_READ          -3    /* Read error */
#define FLOPPY_ERR_WRITE         -4    /* Write error */
#define FLOPPY_ERR_WP            -5    /* Disk is write-protected */
#define FLOPPY_ERR_NO_DISK       -6    /* No disk in drive */
#define FLOPPY_ERR_CRC           -7    /* CRC error */
#define FLOPPY_ERR_NO_SECTOR     -8    /* Sector not found */
#define FLOPPY_ERR_TIMEOUT       -9    /* Operation timed out */

/* Drive state */
typedef struct {
    uint8_t current_track;          /* Current head position (0-79) */
    uint8_t current_side;           /* Current head side (0-1) */
    bool motor_on;                  /* Motor state */
    bool disk_present;              /* Disk in drive */
    bool write_protected;           /* Disk write protect status */
    bool initialized;               /* Drive has been recalibrated */
} floppy_state_t;

/* Track buffer: raw MFM data for one track */
#define MFM_RAW_TRACK_SIZE  12500   /* ~12500 bytes of MFM data per track at 500kbps */
extern uint8_t track_buffer[MFM_RAW_TRACK_SIZE];

/* Decoded sector buffer */
extern uint8_t sector_buffer[FLOPPY_SECTOR_SIZE];

/**
 * Initialize floppy controller GPIO pins and state.
 */
void floppy_init(void);

/**
 * Turn motor on and wait for spin-up.
 */
void floppy_motor_on(void);

/**
 * Turn motor off.
 */
void floppy_motor_off(void);

/**
 * Recalibrate: seek to track 0 using /TRK00 signal.
 * @return FLOPPY_OK on success, error code on failure.
 */
int floppy_recalibrate(void);

/**
 * Seek to specified track.
 * @param track Target track number (0-79).
 * @return FLOPPY_OK on success, error code on failure.
 */
int floppy_seek(uint8_t track);

/**
 * Select head side.
 * @param side 0 = side 0 (top), 1 = side 1 (bottom).
 */
void floppy_select_side(uint8_t side);

/**
 * Read a raw MFM track into track_buffer using PIO/DMA.
 * Motor must be on and head positioned first.
 * @return FLOPPY_OK on success, error code on failure.
 */
int floppy_read_raw_track(void);

/**
 * Read a single sector from disk.
 * @param track  Track number (0-79).
 * @param side   Side (0-1).
 * @param sector Sector number (1-18).
 * @param buf    Buffer to receive 512 bytes of sector data.
 * @return FLOPPY_OK on success, error code on failure.
 */
int floppy_read_sector(uint8_t track, uint8_t side, uint8_t sector, uint8_t *buf);

/**
 * Write a single sector to disk.
 * @param track  Track number (0-79).
 * @param side   Side (0-1).
 * @param sector Sector number (1-18).
 * @param buf    Buffer containing 512 bytes of sector data.
 * @return FLOPPY_OK on success, error code on failure.
 */
int floppy_write_sector(uint8_t track, uint8_t side, uint8_t sector, const uint8_t *buf);

/**
 * Check if a disk is present and not write-protected.
 * @return Current floppy state.
 */
floppy_state_t floppy_get_state(void);

/**
 * Convert a logical sector number (LBA) to track/side/sector (CHS).
 * LBA 0 = track 0, side 0, sector 1.
 * @param lba     Logical sector number.
 * @param track   Output: track number.
 * @param side    Output: side number.
 * @param sector  Output: sector number (1-based).
 */
static inline void floppy_lba_to_chs(uint16_t lba, uint8_t *track, uint8_t *side, uint8_t *sector) {
    *track = lba / (FLOPPY_SIDES * FLOPPY_SECTORS);
    uint16_t temp = lba % (FLOPPY_SIDES * FLOPPY_SECTORS);
    *side = temp / FLOPPY_SECTORS;
    *sector = (temp % FLOPPY_SECTORS) + 1;  /* Sectors are 1-based */
}

/**
 * Convert CHS to LBA.
 */
static inline uint16_t floppy_chs_to_lba(uint8_t track, uint8_t side, uint8_t sector) {
    return (track * FLOPPY_SIDES + side) * FLOPPY_SECTORS + (sector - 1);
}

#endif /* FLOPPY_CTRL_H */
