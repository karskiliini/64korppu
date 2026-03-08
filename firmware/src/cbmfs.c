#include "cbmfs.h"
#include <string.h>

/*
 * CBMFS 1581 filesystem implementation.
 *
 * Logical T/S addressing: track 1-80, sector 0-39, 256 bytes each.
 * System track 40: header (S0), BAM (S1-2), directory (S3+).
 *
 * BAM layout per sector:
 *   Bytes 0-1:   Link to next BAM sector (T/S)
 *   Byte 2:      DOS version 'D'
 *   Byte 3:      Complementary flag (0xBB)
 *   Bytes 4-5:   Disk ID
 *   Bytes 6-15:  Reserved/flags
 *   Bytes 16-255: BAM entries (6 bytes per track)
 *     Byte 0: Free sector count for this track
 *     Bytes 1-5: Bitmap (40 bits, bit=1 = free)
 *
 * BAM sector 1 covers tracks 1-40, BAM sector 2 covers tracks 41-80.
 */

/* Filesystem state */
static struct {
    bool        mounted;
    cbmfs_io_t  io;
    char        disk_name[17];
    char        disk_id[3];
    uint8_t     bam1[CBMFS_SECTOR_SIZE];   /* BAM for tracks 1-40 */
    uint8_t     bam2[CBMFS_SECTOR_SIZE];   /* BAM for tracks 41-80 */
    bool        bam_dirty;
    /* Directory iteration state (shared, single-user) */
    uint8_t     dir_iter_track;
    uint8_t     dir_iter_sector;
    uint8_t     dir_iter_index;
    uint8_t     dir_cache[CBMFS_SECTOR_SIZE];
    bool        dir_cache_valid;
    uint8_t     dir_cache_track;
    uint8_t     dir_cache_sector;
} fs;

/* Internal helpers */

static int io_read(uint8_t track, uint8_t sector, uint8_t *buf) {
    if (!fs.mounted || !fs.io.read_sector) return CBMFS_ERR_IO;
    return fs.io.read_sector(fs.io.context, track, sector, buf);
}

static int io_write(uint8_t track, uint8_t sector, const uint8_t *buf) {
    if (!fs.mounted || !fs.io.write_sector) return CBMFS_ERR_IO;
    return fs.io.write_sector(fs.io.context, track, sector, buf);
}

bool cbmfs_read_sector(uint8_t track, uint8_t sector, uint8_t *buf) {
    if (track < 1 || track > CBMFS_TRACKS) return false;
    if (sector >= CBMFS_SECTORS_PER_TRACK) return false;
    return io_read(track, sector, buf) == 0;
}

bool cbmfs_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf) {
    if (track < 1 || track > CBMFS_TRACKS) return false;
    if (sector >= CBMFS_SECTORS_PER_TRACK) return false;
    return io_write(track, sector, buf) == 0;
}

/*
 * Get the BAM buffer and offset for a given track.
 * @param track     Track number (1-80).
 * @param bam_out   Output: pointer to BAM buffer.
 * @param offset    Output: byte offset within BAM buffer.
 * @return true if valid.
 */
static bool bam_get_entry(uint8_t track, uint8_t **bam_out, uint16_t *offset) {
    if (track < 1 || track > CBMFS_TRACKS) return false;

    if (track <= 40) {
        *bam_out = fs.bam1;
        *offset = CBMFS_BAM_DATA_OFFSET + (track - 1) * CBMFS_BAM_BYTES_PER_TRACK;
    } else {
        *bam_out = fs.bam2;
        *offset = CBMFS_BAM_DATA_OFFSET + (track - 41) * CBMFS_BAM_BYTES_PER_TRACK;
    }
    return true;
}

/*
 * Check if a sector is free in the BAM.
 */
static bool bam_is_free(uint8_t track, uint8_t sector) {
    uint8_t *bam;
    uint16_t offset;
    if (!bam_get_entry(track, &bam, &offset)) return false;

    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);
    return (bam[offset + byte_idx] & bit) != 0;
}

/*
 * Allocate a sector in the BAM.
 */
static void bam_allocate(uint8_t track, uint8_t sector) {
    uint8_t *bam;
    uint16_t offset;
    if (!bam_get_entry(track, &bam, &offset)) return;

    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    if (bam[offset + byte_idx] & bit) {
        bam[offset + byte_idx] &= ~bit;
        bam[offset]--;  /* Decrement free count */
        fs.bam_dirty = true;
    }
}

/*
 * Free a sector in the BAM.
 */
static void bam_free(uint8_t track, uint8_t sector) {
    uint8_t *bam;
    uint16_t offset;
    if (!bam_get_entry(track, &bam, &offset)) return;

    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    if (!(bam[offset + byte_idx] & bit)) {
        bam[offset + byte_idx] |= bit;
        bam[offset]++;  /* Increment free count */
        fs.bam_dirty = true;
    }
}

/*
 * Find a free sector on the disk.
 * Searches outward from the current track.
 * @param alloc_track   Output: track of free sector.
 * @param alloc_sector  Output: sector of free sector.
 * @return true if found.
 */
static bool find_free_block(uint8_t *alloc_track, uint8_t *alloc_sector) {
    /* Search all tracks, skip track 40 (system) */
    for (uint8_t t = 1; t <= CBMFS_TRACKS; t++) {
        if (t == CBMFS_HEADER_TRACK) continue;

        uint8_t *bam;
        uint16_t offset;
        if (!bam_get_entry(t, &bam, &offset)) continue;

        /* Check if track has any free sectors */
        if (bam[offset] == 0) continue;

        for (uint8_t s = 0; s < CBMFS_SECTORS_PER_TRACK; s++) {
            if (bam_is_free(t, s)) {
                *alloc_track = t;
                *alloc_sector = s;
                return true;
            }
        }
    }
    return false;
}

/*
 * Parse a filename from a raw directory entry.
 */
static void parse_dir_filename(const uint8_t *raw_entry, cbmfs_dir_entry_t *entry) {
    /* Filename at offset 5, 16 chars, padded with 0xA0 */
    int len = 16;
    while (len > 0 && raw_entry[5 + len - 1] == 0xA0) len--;
    memcpy(entry->filename, &raw_entry[5], len);
    entry->filename[len] = '\0';
}

/*
 * Read a directory sector, using cache.
 */
static int dir_read_sector(uint8_t track, uint8_t sector) {
    if (fs.dir_cache_valid &&
        fs.dir_cache_track == track && fs.dir_cache_sector == sector) {
        return 0;
    }
    int rc = io_read(track, sector, fs.dir_cache);
    if (rc != 0) return rc;
    fs.dir_cache_valid = true;
    fs.dir_cache_track = track;
    fs.dir_cache_sector = sector;
    return 0;
}

/* Public API */

int cbmfs_mount(const cbmfs_io_t *io) {
    memset(&fs, 0, sizeof(fs));

    fs.io = *io;
    fs.mounted = true;  /* Temporarily, for io_read to work */

    /* Read header sector */
    uint8_t header[CBMFS_SECTOR_SIZE];
    if (io_read(CBMFS_HEADER_TRACK, CBMFS_HEADER_SECTOR, header) != 0) {
        fs.mounted = false;
        return CBMFS_ERR_IO;
    }

    /* Validate: DOS version must be 'D' and DOS type "3D" */
    if (header[CBMFS_HDR_DOS_VERSION] != CBMFS_DOS_VERSION) {
        fs.mounted = false;
        return CBMFS_ERR_INVALID;
    }
    if (header[CBMFS_HDR_DOS_TYPE] != '3' ||
        header[CBMFS_HDR_DOS_TYPE + 1] != 'D') {
        fs.mounted = false;
        return CBMFS_ERR_INVALID;
    }

    /* Read disk name (16 chars, 0xA0 padded) */
    int nlen = 16;
    while (nlen > 0 && header[CBMFS_HDR_DISK_NAME + nlen - 1] == 0xA0) nlen--;
    memcpy(fs.disk_name, &header[CBMFS_HDR_DISK_NAME], nlen);
    fs.disk_name[nlen] = '\0';

    /* Read disk ID */
    fs.disk_id[0] = header[CBMFS_HDR_DISK_ID];
    fs.disk_id[1] = header[CBMFS_HDR_DISK_ID + 1];
    fs.disk_id[2] = '\0';

    /* Read BAM sectors */
    if (io_read(CBMFS_BAM1_TRACK, CBMFS_BAM1_SECTOR, fs.bam1) != 0 ||
        io_read(CBMFS_BAM2_TRACK, CBMFS_BAM2_SECTOR, fs.bam2) != 0) {
        fs.mounted = false;
        return CBMFS_ERR_IO;
    }

    fs.bam_dirty = false;
    return CBMFS_OK;
}

void cbmfs_unmount(void) {
    if (!fs.mounted) return;

    if (fs.bam_dirty) {
        cbmfs_flush_bam();
    }

    fs.mounted = false;
}

bool cbmfs_is_mounted(void) {
    return fs.mounted;
}

void cbmfs_get_disk_name(char *name) {
    if (!fs.mounted) {
        name[0] = '\0';
        return;
    }
    strcpy(name, fs.disk_name);
}

void cbmfs_get_disk_id(char *id) {
    if (!fs.mounted) {
        id[0] = '\0';
        return;
    }
    strcpy(id, fs.disk_id);
}

int cbmfs_flush_bam(void) {
    if (!fs.bam_dirty) return CBMFS_OK;

    if (io_write(CBMFS_BAM1_TRACK, CBMFS_BAM1_SECTOR, fs.bam1) != 0 ||
        io_write(CBMFS_BAM2_TRACK, CBMFS_BAM2_SECTOR, fs.bam2) != 0) {
        return CBMFS_ERR_IO;
    }

    fs.bam_dirty = false;
    return CBMFS_OK;
}

uint16_t cbmfs_free_blocks(void) {
    if (!fs.mounted) return 0;

    uint16_t free = 0;
    for (uint8_t t = 1; t <= CBMFS_TRACKS; t++) {
        if (t == CBMFS_HEADER_TRACK) continue;

        uint8_t *bam;
        uint16_t offset;
        if (bam_get_entry(t, &bam, &offset)) {
            free += bam[offset];
        }
    }
    return free;
}

int cbmfs_dir_first(cbmfs_dir_entry_t *entry) {
    if (!fs.mounted) return -1;

    fs.dir_iter_track = CBMFS_DIR_TRACK;
    fs.dir_iter_sector = CBMFS_DIR_FIRST_SECTOR;
    fs.dir_iter_index = 0;
    fs.dir_cache_valid = false;

    return cbmfs_dir_next(entry);
}

int cbmfs_dir_next(cbmfs_dir_entry_t *entry) {
    if (!fs.mounted) return -1;

    while (fs.dir_iter_track != 0) {
        /* Read current directory sector */
        if (dir_read_sector(fs.dir_iter_track, fs.dir_iter_sector) != 0) {
            return -1;
        }

        while (fs.dir_iter_index < CBMFS_DIR_ENTRIES_PER_SECTOR) {
            uint8_t *raw = &fs.dir_cache[fs.dir_iter_index * CBMFS_DIR_ENTRY_SIZE];
            fs.dir_iter_index++;

            /* Skip empty entries */
            uint8_t file_type = raw[2];
            if (file_type == 0x00) continue;

            /* Skip deleted entries */
            if ((file_type & CBMFS_FILE_TYPE_MASK) == CBMFS_FILE_DEL) continue;

            /* Found a valid entry */
            parse_dir_filename(raw, entry);
            entry->file_type = file_type & CBMFS_FILE_TYPE_MASK;
            entry->first_track = raw[3];
            entry->first_sector = raw[4];
            entry->size_blocks = raw[30] | (raw[31] << 8);

            return 0;
        }

        /* Move to next directory sector via T/S link */
        uint8_t next_track = fs.dir_cache[0];
        uint8_t next_sector = fs.dir_cache[1];

        if (next_track == 0) {
            fs.dir_iter_track = 0;
            break;
        }

        fs.dir_iter_track = next_track;
        fs.dir_iter_sector = next_sector;
        fs.dir_iter_index = 0;
        fs.dir_cache_valid = false;
    }

    return -1;
}

int cbmfs_file_open(const char *name, cbmfs_file_handle_t *handle) {
    if (!fs.mounted) return -1;

    memset(handle, 0, sizeof(*handle));

    /* Search directory for filename */
    cbmfs_dir_entry_t entry;
    int rc = cbmfs_dir_first(&entry);
    while (rc == 0) {
        if (strcmp(entry.filename, name) == 0) {
            handle->active = true;
            handle->write_mode = false;
            handle->cur_track = entry.first_track;
            handle->cur_sector = entry.first_sector;
            handle->buf_pos = 2;  /* Data starts at byte 2 */
            handle->eof = false;

            /* Read first data sector to determine remaining bytes */
            uint8_t sector_buf[CBMFS_SECTOR_SIZE];
            if (!cbmfs_read_sector(handle->cur_track, handle->cur_sector, sector_buf)) {
                handle->active = false;
                return -1;
            }

            /* If next track is 0, this is the last block */
            if (sector_buf[0] == 0) {
                handle->buf_remaining = sector_buf[1];
            } else {
                handle->buf_remaining = 0;  /* Full block */
            }

            return 0;
        }
        rc = cbmfs_dir_next(&entry);
    }

    return -1;  /* Not found */
}

int cbmfs_file_read_byte(cbmfs_file_handle_t *handle) {
    if (!handle->active || handle->eof) return -1;

    /* Read current sector */
    uint8_t sector_buf[CBMFS_SECTOR_SIZE];
    if (!cbmfs_read_sector(handle->cur_track, handle->cur_sector, sector_buf)) {
        handle->eof = true;
        return -1;
    }

    /* Check if this is the last sector */
    bool last_sector = (sector_buf[0] == 0);

    if (last_sector) {
        /* Last sector: data from byte 2 to byte sector_buf[1] */
        if (handle->buf_pos > sector_buf[1]) {
            handle->eof = true;
            return -1;
        }

        uint8_t byte = sector_buf[handle->buf_pos];
        handle->buf_pos++;

        if (handle->buf_pos > sector_buf[1]) {
            handle->eof = true;
        }

        return byte;
    }

    /* Not last sector: full 254 bytes of data (bytes 2-255) */
    uint8_t byte = sector_buf[handle->buf_pos];
    handle->buf_pos++;

    if (handle->buf_pos >= CBMFS_SECTOR_SIZE) {
        /* Move to next sector */
        handle->cur_track = sector_buf[0];
        handle->cur_sector = sector_buf[1];
        handle->buf_pos = 2;

        /* Peek at next sector to check if it's the last */
        uint8_t next_buf[CBMFS_SECTOR_SIZE];
        if (cbmfs_read_sector(handle->cur_track, handle->cur_sector, next_buf)) {
            if (next_buf[0] == 0) {
                handle->buf_remaining = next_buf[1];
            }
        }
    }

    return byte;
}

void cbmfs_file_close(cbmfs_file_handle_t *handle) {
    if (!handle->active) return;

    if (handle->write_mode && handle->write_pos > 2) {
        /* Flush remaining write buffer as last block */
        handle->write_buf[0] = 0;  /* No next block */
        handle->write_buf[1] = handle->write_pos - 1;  /* Last byte position */

        /* Write the buffer to the current block */
        cbmfs_write_sector(handle->cur_track, handle->cur_sector, handle->write_buf);
        handle->blocks_written++;

        /* Update directory entry with block count */
        /* Find our entry in directory */
        uint8_t dir_track = CBMFS_DIR_TRACK;
        uint8_t dir_sector = CBMFS_DIR_FIRST_SECTOR;
        uint8_t dir_buf[CBMFS_SECTOR_SIZE];

        while (dir_track != 0) {
            if (!cbmfs_read_sector(dir_track, dir_sector, dir_buf)) break;

            for (int i = 0; i < CBMFS_DIR_ENTRIES_PER_SECTOR; i++) {
                uint8_t *raw = &dir_buf[i * CBMFS_DIR_ENTRY_SIZE];
                if (raw[3] == handle->first_track &&
                    raw[4] == handle->first_sector &&
                    (raw[2] & CBMFS_FILE_TYPE_MASK) != CBMFS_FILE_DEL) {
                    /* Update block count and mark as closed */
                    raw[30] = handle->blocks_written & 0xFF;
                    raw[31] = (handle->blocks_written >> 8) & 0xFF;
                    raw[2] |= CBMFS_FILE_CLOSED;
                    cbmfs_write_sector(dir_track, dir_sector, dir_buf);
                    goto dir_updated;
                }
            }

            uint8_t next_t = dir_buf[0];
            uint8_t next_s = dir_buf[1];
            if (next_t == 0) break;
            dir_track = next_t;
            dir_sector = next_s;
        }
    dir_updated:

        cbmfs_flush_bam();
    }

    handle->active = false;
}

int cbmfs_file_create(const char *name, cbmfs_file_handle_t *handle) {
    if (!fs.mounted) return -1;

    memset(handle, 0, sizeof(*handle));

    /* Find a free directory entry */
    uint8_t dir_track = CBMFS_DIR_TRACK;
    uint8_t dir_sector = CBMFS_DIR_FIRST_SECTOR;
    uint8_t dir_buf[CBMFS_SECTOR_SIZE];

    while (dir_track != 0) {
        if (!cbmfs_read_sector(dir_track, dir_sector, dir_buf)) return -1;

        for (int i = 0; i < CBMFS_DIR_ENTRIES_PER_SECTOR; i++) {
            uint8_t *raw = &dir_buf[i * CBMFS_DIR_ENTRY_SIZE];
            uint8_t file_type = raw[2];

            /* Empty or deleted slot */
            if (file_type == 0x00 ||
                (file_type & CBMFS_FILE_TYPE_MASK) == CBMFS_FILE_DEL) {

                /* Allocate first data block */
                uint8_t first_t, first_s;
                if (!find_free_block(&first_t, &first_s)) return -1;
                bam_allocate(first_t, first_s);

                /* Fill directory entry */
                memset(raw, 0, CBMFS_DIR_ENTRY_SIZE);
                raw[2] = CBMFS_FILE_PRG;  /* PRG, not closed yet */
                raw[3] = first_t;
                raw[4] = first_s;

                /* Filename padded with 0xA0 */
                memset(&raw[5], 0xA0, 16);
                int nlen = strlen(name);
                if (nlen > 16) nlen = 16;
                memcpy(&raw[5], name, nlen);

                /* Write directory sector */
                cbmfs_write_sector(dir_track, dir_sector, dir_buf);
                fs.dir_cache_valid = false;

                /* Initialize handle */
                handle->active = true;
                handle->write_mode = true;
                handle->cur_track = first_t;
                handle->cur_sector = first_s;
                handle->first_track = first_t;
                handle->first_sector = first_s;
                handle->write_pos = 2;  /* Data starts at byte 2 */
                handle->blocks_written = 0;

                /* Initialize write buffer */
                memset(handle->write_buf, 0, CBMFS_SECTOR_SIZE);

                return 0;
            }
        }

        /* Follow chain to next directory sector */
        uint8_t next_t = dir_buf[0];
        uint8_t next_s = dir_buf[1];
        if (next_t == 0) break;
        dir_track = next_t;
        dir_sector = next_s;
    }

    return -1;  /* Directory full */
}

int cbmfs_file_write_byte(cbmfs_file_handle_t *handle, uint8_t byte) {
    if (!handle->active || !handle->write_mode) return -1;

    /* If current block is full from previous write, flush and start new block */
    if (handle->write_pos >= CBMFS_SECTOR_SIZE) {
        uint8_t next_t, next_s;
        if (!find_free_block(&next_t, &next_s)) return -1;
        bam_allocate(next_t, next_s);

        /* Set link to next block */
        handle->write_buf[0] = next_t;
        handle->write_buf[1] = next_s;

        /* Write current block */
        cbmfs_write_sector(handle->cur_track, handle->cur_sector, handle->write_buf);
        handle->blocks_written++;

        /* Move to next block */
        handle->cur_track = next_t;
        handle->cur_sector = next_s;
        handle->write_pos = 2;
        memset(handle->write_buf, 0, CBMFS_SECTOR_SIZE);
    }

    handle->write_buf[handle->write_pos++] = byte;
    return 0;
}

int cbmfs_file_delete(const char *name) {
    if (!fs.mounted) return -1;

    uint8_t dir_track = CBMFS_DIR_TRACK;
    uint8_t dir_sector = CBMFS_DIR_FIRST_SECTOR;
    uint8_t dir_buf[CBMFS_SECTOR_SIZE];

    while (dir_track != 0) {
        if (!cbmfs_read_sector(dir_track, dir_sector, dir_buf)) return -1;

        for (int i = 0; i < CBMFS_DIR_ENTRIES_PER_SECTOR; i++) {
            uint8_t *raw = &dir_buf[i * CBMFS_DIR_ENTRY_SIZE];
            uint8_t file_type = raw[2];

            if (file_type == 0x00) continue;
            if ((file_type & CBMFS_FILE_TYPE_MASK) == CBMFS_FILE_DEL) continue;

            /* Parse and compare filename */
            cbmfs_dir_entry_t entry;
            parse_dir_filename(raw, &entry);

            if (strcmp(entry.filename, name) == 0) {
                /* Free all blocks in the T/S chain */
                uint8_t t = raw[3];
                uint8_t s = raw[4];
                uint8_t block_buf[CBMFS_SECTOR_SIZE];

                while (t != 0) {
                    bam_free(t, s);

                    if (!cbmfs_read_sector(t, s, block_buf)) break;

                    uint8_t next_t = block_buf[0];
                    uint8_t next_s = block_buf[1];
                    t = next_t;
                    s = (t == 0) ? 0 : next_s;
                }

                /* Mark directory entry as deleted */
                raw[2] = CBMFS_FILE_DEL;
                cbmfs_write_sector(dir_track, dir_sector, dir_buf);
                fs.dir_cache_valid = false;

                cbmfs_flush_bam();
                return 0;
            }
        }

        uint8_t next_t = dir_buf[0];
        uint8_t next_s = dir_buf[1];
        if (next_t == 0) break;
        dir_track = next_t;
        dir_sector = next_s;
    }

    return -1;  /* Not found */
}

int cbmfs_format(const cbmfs_io_t *io, const char *name, const char *id) {
    /* Temporarily mount for I/O access */
    cbmfs_io_t saved_io = fs.io;
    bool was_mounted = fs.mounted;

    fs.io = *io;
    fs.mounted = true;

    /* Zero all sectors on track 40 */
    uint8_t zero[CBMFS_SECTOR_SIZE];
    memset(zero, 0, CBMFS_SECTOR_SIZE);

    for (uint8_t s = 0; s < CBMFS_SECTORS_PER_TRACK; s++) {
        io_write(CBMFS_HEADER_TRACK, s, zero);
    }

    /* Write header sector (T40/S0) */
    uint8_t header[CBMFS_SECTOR_SIZE];
    memset(header, 0, CBMFS_SECTOR_SIZE);

    /* Directory pointer */
    header[CBMFS_HDR_DIR_TRACK] = CBMFS_DIR_TRACK;
    header[CBMFS_HDR_DIR_SECTOR] = CBMFS_DIR_FIRST_SECTOR;

    /* DOS version */
    header[CBMFS_HDR_DOS_VERSION] = CBMFS_DOS_VERSION;

    /* Disk name (padded with 0xA0) */
    memset(&header[CBMFS_HDR_DISK_NAME], 0xA0, 16);
    int nlen = name ? (int)strlen(name) : 0;
    if (nlen > 16) nlen = 16;
    if (nlen > 0) memcpy(&header[CBMFS_HDR_DISK_NAME], name, nlen);

    /* Padding between name and ID */
    header[20] = 0xA0;
    header[21] = 0xA0;

    /* Disk ID */
    if (id && id[0] && id[1]) {
        header[CBMFS_HDR_DISK_ID] = id[0];
        header[CBMFS_HDR_DISK_ID + 1] = id[1];
    } else {
        header[CBMFS_HDR_DISK_ID] = '6';
        header[CBMFS_HDR_DISK_ID + 1] = '4';
    }

    /* Padding and DOS type */
    header[24] = 0xA0;
    header[CBMFS_HDR_DOS_TYPE] = '3';
    header[CBMFS_HDR_DOS_TYPE + 1] = 'D';

    io_write(CBMFS_HEADER_TRACK, CBMFS_HEADER_SECTOR, header);

    /* Write BAM sector 1 (tracks 1-40) */
    uint8_t bam[CBMFS_SECTOR_SIZE];
    memset(bam, 0, CBMFS_SECTOR_SIZE);

    /* Link to BAM sector 2 */
    bam[0] = CBMFS_BAM2_TRACK;
    bam[1] = CBMFS_BAM2_SECTOR;
    bam[2] = CBMFS_DOS_VERSION;
    bam[3] = 0xBB;  /* Complementary flag */
    bam[4] = header[CBMFS_HDR_DISK_ID];
    bam[5] = header[CBMFS_HDR_DISK_ID + 1];

    /* Initialize BAM entries for tracks 1-40 */
    for (uint8_t t = 1; t <= 40; t++) {
        uint16_t off = CBMFS_BAM_DATA_OFFSET + (t - 1) * CBMFS_BAM_BYTES_PER_TRACK;

        if (t == CBMFS_HEADER_TRACK) {
            /* Track 40: all sectors allocated (system track) */
            bam[off] = 0;
            bam[off + 1] = 0;
            bam[off + 2] = 0;
            bam[off + 3] = 0;
            bam[off + 4] = 0;
            bam[off + 5] = 0;
        } else {
            /* All 40 sectors free */
            bam[off] = 40;
            bam[off + 1] = 0xFF;
            bam[off + 2] = 0xFF;
            bam[off + 3] = 0xFF;
            bam[off + 4] = 0xFF;
            bam[off + 5] = 0xFF;
        }
    }

    io_write(CBMFS_BAM1_TRACK, CBMFS_BAM1_SECTOR, bam);

    /* Write BAM sector 2 (tracks 41-80) */
    memset(bam, 0, CBMFS_SECTOR_SIZE);
    bam[0] = 0;     /* No more BAM sectors */
    bam[1] = 0xFF;
    bam[2] = CBMFS_DOS_VERSION;
    bam[3] = 0xBB;
    bam[4] = header[CBMFS_HDR_DISK_ID];
    bam[5] = header[CBMFS_HDR_DISK_ID + 1];

    for (uint8_t t = 41; t <= 80; t++) {
        uint16_t off = CBMFS_BAM_DATA_OFFSET + (t - 41) * CBMFS_BAM_BYTES_PER_TRACK;
        bam[off] = 40;
        bam[off + 1] = 0xFF;
        bam[off + 2] = 0xFF;
        bam[off + 3] = 0xFF;
        bam[off + 4] = 0xFF;
        bam[off + 5] = 0xFF;
    }

    io_write(CBMFS_BAM2_TRACK, CBMFS_BAM2_SECTOR, bam);

    /* Write first directory sector (T40/S3): empty, end of chain */
    memset(zero, 0, CBMFS_SECTOR_SIZE);
    zero[1] = 0xFF;  /* No next sector */
    io_write(CBMFS_DIR_TRACK, CBMFS_DIR_FIRST_SECTOR, zero);

    /* Restore state */
    if (!was_mounted) {
        fs.mounted = false;
        fs.io = saved_io;
    }

    return CBMFS_OK;
}
