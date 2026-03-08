#ifndef CBMFS_H
#define CBMFS_H

#include <stdint.h>
#include <stdbool.h>

/*
 * CBMFS - Commodore 1581 compatible filesystem.
 *
 * Logical layout (same as D81 disk image):
 *   80 tracks (1-80), 40 sectors per track (0-39), 256 bytes per sector
 *   Total: 3200 sectors, 819200 bytes, 3160 data blocks available
 *
 * Physical layout on 3.5" DD disk:
 *   80 tracks, 2 sides, 10 sectors/track, 512 bytes/sector
 *   Each 512-byte physical sector holds 2 logical 256-byte sectors
 *
 * System track (track 40):
 *   Sector 0:   Header (disk name, ID, DOS type "3D")
 *   Sectors 1-2: BAM (Block Availability Map)
 *   Sector 3+:  Directory (linked T/S chain, 8 entries per sector)
 *
 * I/O abstraction (cbmfs_io_t) allows the same code to work with:
 *   - Native DD floppy disk (via floppy_ctrl)
 *   - D81 disk image file (future, via FAT12)
 *   - In-memory buffer (for testing)
 */

/* 1581 logical geometry */
#define CBMFS_TRACKS             80
#define CBMFS_SECTORS_PER_TRACK  40
#define CBMFS_SECTOR_SIZE       256
#define CBMFS_TOTAL_SECTORS    3200
#define CBMFS_IMAGE_SIZE     819200

/* System track */
#define CBMFS_HEADER_TRACK       40
#define CBMFS_HEADER_SECTOR       0
#define CBMFS_BAM1_TRACK         40
#define CBMFS_BAM1_SECTOR         1
#define CBMFS_BAM2_TRACK         40
#define CBMFS_BAM2_SECTOR         2
#define CBMFS_DIR_TRACK          40
#define CBMFS_DIR_FIRST_SECTOR    3

/* Directory */
#define CBMFS_DIR_ENTRIES_PER_SECTOR  8
#define CBMFS_DIR_ENTRY_SIZE         32

/* Data blocks available: 3200 total - 40 (track 40) = 3160 */
#define CBMFS_DATA_BLOCKS       3160

/* File types (same as D64) */
#define CBMFS_FILE_DEL    0x00
#define CBMFS_FILE_SEQ    0x01
#define CBMFS_FILE_PRG    0x02
#define CBMFS_FILE_USR    0x03
#define CBMFS_FILE_REL    0x04
#define CBMFS_FILE_TYPE_MASK  0x07
#define CBMFS_FILE_CLOSED     0x80

/* BAM layout: each track uses 6 bytes (1 free count + 5 bitmap bytes) */
#define CBMFS_BAM_BYTES_PER_TRACK  6
#define CBMFS_BAM_DATA_OFFSET     16  /* BAM entries start at byte 16 */

/* Header sector offsets */
#define CBMFS_HDR_DIR_TRACK     0   /* T/S of first dir sector */
#define CBMFS_HDR_DIR_SECTOR    1
#define CBMFS_HDR_DOS_VERSION   2   /* 'D' = 0x44 */
#define CBMFS_HDR_DISK_NAME     4   /* 16 chars, 0xA0 padded */
#define CBMFS_HDR_DISK_ID      22   /* 2 chars */
#define CBMFS_HDR_DOS_TYPE     25   /* "3D" */

/* DOS version byte */
#define CBMFS_DOS_VERSION      0x44  /* 'D' */

/* I/O abstraction: read/write logical 256-byte sectors */
typedef struct {
    int (*read_sector)(void *ctx, uint8_t track, uint8_t sector, uint8_t *buf);
    int (*write_sector)(void *ctx, uint8_t track, uint8_t sector, const uint8_t *buf);
    void *context;
} cbmfs_io_t;

/* Directory entry (parsed) */
typedef struct {
    char    filename[17];       /* 16 chars + null */
    uint8_t file_type;          /* CBMFS_FILE_PRG, etc. */
    uint8_t first_track;
    uint8_t first_sector;
    uint16_t size_blocks;
} cbmfs_dir_entry_t;

/* File handle */
typedef struct {
    bool    active;
    bool    write_mode;
    uint8_t cur_track;
    uint8_t cur_sector;
    uint16_t buf_pos;           /* Position within current sector (2-256) */
    uint8_t buf_remaining;      /* Bytes remaining in last block */
    bool    eof;
    /* Directory iteration state */
    uint8_t dir_track;
    uint8_t dir_sector;
    uint8_t dir_entry_index;
    /* Write mode state */
    uint8_t first_track;
    uint8_t first_sector;
    uint16_t blocks_written;
    uint8_t write_buf[256];
    uint16_t write_pos;
} cbmfs_file_handle_t;

/* Error codes */
#define CBMFS_OK              0
#define CBMFS_ERR_IO         -1
#define CBMFS_ERR_NOT_MOUNT  -2
#define CBMFS_ERR_NOT_FOUND  -3
#define CBMFS_ERR_DISK_FULL  -4
#define CBMFS_ERR_DIR_FULL   -5
#define CBMFS_ERR_INVALID    -6

/**
 * Mount a CBMFS filesystem.
 * Reads header and BAM sectors.
 * @param io  I/O callbacks for sector access.
 * @return CBMFS_OK on success, error code on failure.
 */
int cbmfs_mount(const cbmfs_io_t *io);

/**
 * Unmount. Flushes BAM if dirty.
 */
void cbmfs_unmount(void);

/**
 * Check if mounted.
 */
bool cbmfs_is_mounted(void);

/**
 * Get disk name from header.
 * @param name  Output buffer (at least 17 bytes).
 */
void cbmfs_get_disk_name(char *name);

/**
 * Get disk ID (2 chars).
 * @param id  Output buffer (at least 3 bytes).
 */
void cbmfs_get_disk_id(char *id);

/**
 * Start iterating directory.
 * @param entry  Output: first directory entry.
 * @return 0 on success, -1 if no entries.
 */
int cbmfs_dir_first(cbmfs_dir_entry_t *entry);

/**
 * Get next directory entry.
 * @param entry  Output: next entry.
 * @return 0 on success, -1 if no more.
 */
int cbmfs_dir_next(cbmfs_dir_entry_t *entry);

/**
 * Open a file for reading.
 * @param name    Filename (up to 16 chars).
 * @param handle  File handle to initialize.
 * @return 0 on success, -1 if not found.
 */
int cbmfs_file_open(const char *name, cbmfs_file_handle_t *handle);

/**
 * Read next byte from open file.
 * @param handle  File handle.
 * @return Byte value (0-255), or -1 on EOF/error.
 */
int cbmfs_file_read_byte(cbmfs_file_handle_t *handle);

/**
 * Close file handle.
 */
void cbmfs_file_close(cbmfs_file_handle_t *handle);

/**
 * Create a new PRG file for writing.
 * @param name    Filename (up to 16 chars).
 * @param handle  File handle to initialize.
 * @return 0 on success, -1 on error.
 */
int cbmfs_file_create(const char *name, cbmfs_file_handle_t *handle);

/**
 * Write a byte to file.
 * @param handle  File handle.
 * @param byte    Byte to write.
 * @return 0 on success, -1 on error.
 */
int cbmfs_file_write_byte(cbmfs_file_handle_t *handle, uint8_t byte);

/**
 * Delete a file.
 * @param name  Filename.
 * @return 0 on success, -1 if not found.
 */
int cbmfs_file_delete(const char *name);

/**
 * Count free blocks.
 * @return Number of free blocks.
 */
uint16_t cbmfs_free_blocks(void);

/**
 * Format a disk as CBMFS 1581.
 * @param io     I/O callbacks.
 * @param name   Disk name (up to 16 chars).
 * @param id     2-char disk ID.
 * @return CBMFS_OK on success.
 */
int cbmfs_format(const cbmfs_io_t *io, const char *name, const char *id);

/**
 * Flush BAM to disk.
 * @return CBMFS_OK on success.
 */
int cbmfs_flush_bam(void);

/**
 * Read a logical sector from the CBMFS.
 * @param track   Track (1-80).
 * @param sector  Sector (0-39).
 * @param buf     Output buffer (256 bytes).
 * @return true on success.
 */
bool cbmfs_read_sector(uint8_t track, uint8_t sector, uint8_t *buf);

/**
 * Write a logical sector.
 * @param track   Track (1-80).
 * @param sector  Sector (0-39).
 * @param buf     Input buffer (256 bytes).
 * @return true on success.
 */
bool cbmfs_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf);

#endif /* CBMFS_H */
