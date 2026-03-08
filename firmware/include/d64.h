#ifndef D64_H
#define D64_H

#include <stdint.h>
#include <stdbool.h>

/*
 * D64 disk image support.
 *
 * Standard 1541 disk image (35 tracks, 683 sectors, 174848 bytes).
 * The entire image is loaded into RAM for fast access.
 *
 * Track layout:
 *   Tracks  1-17: 21 sectors each
 *   Tracks 18-24: 19 sectors each
 *   Tracks 25-30: 18 sectors each
 *   Tracks 31-35: 17 sectors each
 *
 * Track 18, sector 0: BAM (Block Availability Map) + disk name
 * Track 18, sector 1+: Directory (linked list, 8 entries per sector)
 *
 * Data blocks: byte 0-1 = next T/S (0/0 = last), bytes 2-255 = 254 bytes data
 */

#define D64_IMAGE_SIZE      174848
#define D64_SECTOR_SIZE     256
#define D64_NUM_TRACKS      35
#define D64_TOTAL_SECTORS   683

#define D64_BAM_TRACK       18
#define D64_BAM_SECTOR      0
#define D64_DIR_TRACK       18
#define D64_DIR_FIRST_SECTOR 1

#define D64_DIR_ENTRIES_PER_SECTOR  8
#define D64_DIR_ENTRY_SIZE          32

/* CBM file types */
#define D64_FILE_DEL    0x00
#define D64_FILE_SEQ    0x01
#define D64_FILE_PRG    0x02
#define D64_FILE_USR    0x03
#define D64_FILE_REL    0x04
#define D64_FILE_TYPE_MASK  0x07
#define D64_FILE_CLOSED     0x80    /* Bit 7 = file properly closed */

/* Directory entry parsed from D64 image */
typedef struct {
    char    filename[17];       /* 16 chars + null terminator */
    uint8_t file_type;          /* D64_FILE_PRG, etc. */
    uint8_t first_track;
    uint8_t first_sector;
    uint16_t size_blocks;       /* Size in 256-byte blocks */
} d64_dir_entry_t;

/* File handle for reading/writing files within a D64 image */
typedef struct {
    bool    active;
    bool    write_mode;
    uint8_t cur_track;          /* Current data block track */
    uint8_t cur_sector;         /* Current data block sector */
    uint16_t buf_pos;           /* Position within current block (2-256) */
    uint8_t buf_remaining;      /* Bytes remaining in last block */
    bool    eof;
    /* For directory iteration */
    uint8_t dir_track;
    uint8_t dir_sector;
    uint8_t dir_entry_index;    /* 0-7 within current dir sector */
    /* For write mode */
    uint8_t first_track;
    uint8_t first_sector;
    uint16_t blocks_written;
    uint8_t write_buf[256];     /* Accumulate current block */
    uint16_t write_pos;         /* Position in write_buf (2-256) */
} d64_file_handle_t;

/**
 * Mount a D64 image from the FAT12 filesystem into RAM.
 * @param filename  D64 filename on FAT12 (CBM-style, e.g. "GAME.D64").
 * @return true on success.
 */
bool d64_mount(const char *filename);

/**
 * Flush modified D64 image back to FAT12 disk.
 * @return true on success.
 */
bool d64_flush(void);

/**
 * Unmount D64 image. Flushes if dirty.
 */
void d64_unmount(void);

/**
 * Check if a D64 image is currently mounted.
 */
bool d64_is_mounted(void);

/**
 * Check if the mounted D64 has unsaved changes.
 */
bool d64_is_dirty(void);

/**
 * Get the disk name from the mounted D64's BAM.
 * @param name  Output buffer (at least 17 bytes).
 */
void d64_get_disk_name(char *name);

/**
 * Read a raw sector from the D64 image in RAM.
 * @param track   Track number (1-35).
 * @param sector  Sector number.
 * @param buf     Output buffer (256 bytes).
 * @return true on success.
 */
bool d64_read_sector(uint8_t track, uint8_t sector, uint8_t *buf);

/**
 * Write a raw sector to the D64 image in RAM.
 * @param track   Track number (1-35).
 * @param sector  Sector number.
 * @param buf     Input buffer (256 bytes).
 * @return true on success.
 */
bool d64_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf);

/**
 * Start iterating the D64 directory.
 * @param entry  Output: first directory entry.
 * @return 0 on success, -1 if no entries.
 */
int d64_dir_first(d64_dir_entry_t *entry);

/**
 * Get next directory entry.
 * @param entry  Output: next directory entry.
 * @return 0 on success, -1 if no more entries.
 */
int d64_dir_next(d64_dir_entry_t *entry);

/**
 * Open a PRG file within the D64 image for reading.
 * Follows the T/S chain from the directory entry.
 * @param name    Filename (CBM PETSCII, up to 16 chars).
 * @param handle  File handle to initialize.
 * @return 0 on success, -1 if not found.
 */
int d64_file_open(const char *name, d64_file_handle_t *handle);

/**
 * Read the next byte from an open D64 file.
 * @param handle  File handle.
 * @return Byte value (0-255), or -1 on EOF/error.
 */
int d64_file_read_byte(d64_file_handle_t *handle);

/**
 * Close an open D64 file handle.
 */
void d64_file_close(d64_file_handle_t *handle);

/**
 * Create a new PRG file within the D64 image for writing.
 * @param name    Filename (up to 16 chars).
 * @param handle  File handle to initialize.
 * @return 0 on success, -1 on error (disk full, dir full).
 */
int d64_file_create(const char *name, d64_file_handle_t *handle);

/**
 * Write a byte to a D64 file being created.
 * @param handle  File handle.
 * @param byte    Byte to write.
 * @return 0 on success, -1 on error (disk full).
 */
int d64_file_write_byte(d64_file_handle_t *handle, uint8_t byte);

/**
 * Delete a file from the D64 image.
 * Frees blocks in BAM and marks directory entry as deleted.
 * @param name  Filename to delete.
 * @return 0 on success, -1 if not found.
 */
int d64_file_delete(const char *name);

#endif /* D64_H */
