#include "d64.h"
#include "fat12.h"
#include <string.h>
#include <ctype.h>

/*
 * D64 disk image handler.
 *
 * The entire 174,848-byte D64 image is kept in RAM for fast access.
 * Modifications are tracked with a dirty flag and flushed to FAT12
 * on unmount or explicit flush command.
 */

/* D64 image buffer in RAM */
static uint8_t d64_image[D64_IMAGE_SIZE];
static bool d64_mounted = false;
static bool d64_dirty = false;
static char d64_disk_name[17];

/* FAT12 file info for flush-back */
static char d64_fat12_name[9];
static char d64_fat12_ext[4];

/* Sectors per track lookup (index 0 unused, tracks 1-35) */
static const uint8_t sectors_per_track[36] = {
    0,                                              /* track 0 (unused) */
    21, 21, 21, 21, 21, 21, 21, 21, 21, 21,        /* tracks 1-10 */
    21, 21, 21, 21, 21, 21, 21,                     /* tracks 11-17 */
    19, 19, 19, 19, 19, 19, 19,                     /* tracks 18-24 */
    18, 18, 18, 18, 18, 18,                         /* tracks 25-30 */
    17, 17, 17, 17, 17                              /* tracks 31-35 */
};

/* Directory iteration state (shared between dir_first/dir_next) */
static uint8_t iter_dir_track;
static uint8_t iter_dir_sector;
static uint8_t iter_dir_index;

/*
 * Convert track/sector to linear byte offset within the D64 image.
 * Returns -1 on invalid track/sector.
 */
static int32_t ts_to_offset(uint8_t track, uint8_t sector) {
    if (track < 1 || track > D64_NUM_TRACKS) return -1;
    if (sector >= sectors_per_track[track]) return -1;

    int32_t offset = 0;
    for (uint8_t t = 1; t < track; t++) {
        offset += sectors_per_track[t] * D64_SECTOR_SIZE;
    }
    offset += sector * D64_SECTOR_SIZE;

    return offset;
}

bool d64_read_sector(uint8_t track, uint8_t sector, uint8_t *buf) {
    int32_t offset = ts_to_offset(track, sector);
    if (offset < 0) return false;
    memcpy(buf, &d64_image[offset], D64_SECTOR_SIZE);
    return true;
}

bool d64_write_sector(uint8_t track, uint8_t sector, const uint8_t *buf) {
    int32_t offset = ts_to_offset(track, sector);
    if (offset < 0) return false;
    memcpy(&d64_image[offset], buf, D64_SECTOR_SIZE);
    d64_dirty = true;
    return true;
}

/*
 * Get a direct pointer to a sector within the RAM image.
 */
static uint8_t *sector_ptr(uint8_t track, uint8_t sector) {
    int32_t offset = ts_to_offset(track, sector);
    if (offset < 0) return NULL;
    return &d64_image[offset];
}

bool d64_mount(const char *filename) {
    if (d64_mounted) {
        d64_unmount();
    }

    /* Parse filename to FAT12 8.3 format */
    fat12_parse_filename(filename, d64_fat12_name, d64_fat12_ext);

    /* Open the D64 file from FAT12 */
    fat12_file_t file;
    int rc = fat12_open_read(d64_fat12_name, d64_fat12_ext, &file);
    if (rc != FAT12_OK) return false;

    /* Verify file size */
    if (file.file_size != D64_IMAGE_SIZE) {
        fat12_close(&file);
        return false;
    }

    /* Read entire D64 image into RAM (in 512-byte chunks to fit uint16_t count) */
    int total_read = 0;
    while (total_read < D64_IMAGE_SIZE) {
        int remaining = D64_IMAGE_SIZE - total_read;
        uint16_t chunk = (remaining > 512) ? 512 : (uint16_t)remaining;
        int n = fat12_read(&file, &d64_image[total_read], chunk);
        if (n <= 0) {
            fat12_close(&file);
            return false;
        }
        total_read += n;
    }

    fat12_close(&file);

    /* Extract disk name from BAM (track 18, sector 0, offset 0x90) */
    uint8_t *bam = sector_ptr(D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return false;

    memset(d64_disk_name, 0, sizeof(d64_disk_name));
    for (int i = 0; i < 16; i++) {
        uint8_t ch = bam[0x90 + i];
        if (ch == 0xA0) break;  /* 0xA0 = shifted space (padding) */
        d64_disk_name[i] = ch;
    }

    d64_mounted = true;
    d64_dirty = false;

    return true;
}

bool d64_flush(void) {
    if (!d64_mounted || !d64_dirty) return true;

    /* Delete existing file and recreate with updated content */
    fat12_delete(d64_fat12_name, d64_fat12_ext);

    fat12_file_t file;
    int rc = fat12_create(d64_fat12_name, d64_fat12_ext, &file);
    if (rc != FAT12_OK) return false;

    /* Write entire image back (in 512-byte chunks to fit uint16_t count) */
    int total_written = 0;
    while (total_written < D64_IMAGE_SIZE) {
        int remaining = D64_IMAGE_SIZE - total_written;
        uint16_t chunk = (remaining > 512) ? 512 : (uint16_t)remaining;
        int n = fat12_write(&file, &d64_image[total_written], chunk);
        if (n <= 0) {
            fat12_close(&file);
            return false;
        }
        total_written += n;
    }

    rc = fat12_close(&file);
    if (rc != FAT12_OK) return false;

    d64_dirty = false;
    return true;
}

void d64_unmount(void) {
    if (!d64_mounted) return;

    if (d64_dirty) {
        d64_flush();
    }

    d64_mounted = false;
    d64_dirty = false;
}

bool d64_is_mounted(void) {
    return d64_mounted;
}

bool d64_is_dirty(void) {
    return d64_dirty;
}

void d64_get_disk_name(char *name) {
    if (d64_mounted) {
        memcpy(name, d64_disk_name, 17);
    } else {
        memset(name, 0, 17);
    }
}

/*
 * Parse a D64 directory entry from raw 32-byte data.
 */
static bool parse_dir_entry(const uint8_t *raw, d64_dir_entry_t *entry) {
    uint8_t file_type = raw[2];

    /* Skip empty/deleted entries */
    if ((file_type & D64_FILE_TYPE_MASK) == D64_FILE_DEL) return false;
    if (!(file_type & D64_FILE_CLOSED)) return false;

    entry->file_type = file_type & D64_FILE_TYPE_MASK;
    entry->first_track = raw[3];
    entry->first_sector = raw[4];

    /* Extract filename (bytes 5-20), trim 0xA0 padding */
    memset(entry->filename, 0, sizeof(entry->filename));
    for (int i = 0; i < 16; i++) {
        uint8_t ch = raw[5 + i];
        if (ch == 0xA0) break;
        entry->filename[i] = ch;
    }

    /* Size in blocks (little-endian at bytes 30-31) */
    entry->size_blocks = raw[30] | (raw[31] << 8);

    return true;
}

int d64_dir_first(d64_dir_entry_t *entry) {
    if (!d64_mounted) return -1;

    /* Start at track 18, sector 1 */
    iter_dir_track = D64_DIR_TRACK;
    iter_dir_sector = D64_DIR_FIRST_SECTOR;
    iter_dir_index = 0;

    return d64_dir_next(entry);
}

int d64_dir_next(d64_dir_entry_t *entry) {
    if (!d64_mounted) return -1;

    while (iter_dir_track != 0) {
        uint8_t *sec = sector_ptr(iter_dir_track, iter_dir_sector);
        if (!sec) return -1;

        while (iter_dir_index < D64_DIR_ENTRIES_PER_SECTOR) {
            const uint8_t *raw = &sec[iter_dir_index * D64_DIR_ENTRY_SIZE];
            iter_dir_index++;

            if (parse_dir_entry(raw, entry)) {
                return 0;
            }
        }

        /* Follow directory chain */
        uint8_t next_track = sec[0];
        uint8_t next_sector = sec[1];

        if (next_track == 0) {
            iter_dir_track = 0;
            return -1;
        }

        iter_dir_track = next_track;
        iter_dir_sector = next_sector;
        iter_dir_index = 0;
    }

    return -1;
}

/*
 * Compare a search name with a D64 directory filename.
 * D64 filenames are PETSCII, up to 16 chars, padded with 0xA0.
 * The search name is uppercase ASCII (from CBM-DOS command parsing).
 */
static bool name_match(const uint8_t *dir_name, const char *search, uint8_t search_len) {
    for (int i = 0; i < 16; i++) {
        uint8_t dir_ch = dir_name[i];
        if (dir_ch == 0xA0) dir_ch = 0;

        if (i >= search_len) {
            /* Search name exhausted — match if dir name also ends */
            return (dir_ch == 0);
        }

        uint8_t search_ch = toupper((unsigned char)search[i]);
        if (dir_ch != search_ch) return false;
    }

    return (search_len <= 16);
}

int d64_file_open(const char *name, d64_file_handle_t *handle) {
    if (!d64_mounted || !handle) return -1;

    memset(handle, 0, sizeof(*handle));

    uint8_t name_len = strlen(name);
    uint8_t dir_track = D64_DIR_TRACK;
    uint8_t dir_sector = D64_DIR_FIRST_SECTOR;

    while (dir_track != 0) {
        uint8_t *sec = sector_ptr(dir_track, dir_sector);
        if (!sec) return -1;

        for (int i = 0; i < D64_DIR_ENTRIES_PER_SECTOR; i++) {
            const uint8_t *raw = &sec[i * D64_DIR_ENTRY_SIZE];
            uint8_t ftype = raw[2];

            if ((ftype & D64_FILE_TYPE_MASK) == D64_FILE_DEL) continue;
            if (!(ftype & D64_FILE_CLOSED)) continue;

            if (name_match(&raw[5], name, name_len)) {
                handle->active = true;
                handle->write_mode = false;
                handle->cur_track = raw[3];
                handle->cur_sector = raw[4];
                handle->buf_pos = 2;  /* Data starts at byte 2 */
                handle->eof = false;
                return 0;
            }
        }

        /* Follow chain */
        uint8_t next_t = sec[0];
        uint8_t next_s = sec[1];
        if (next_t == 0) break;
        dir_track = next_t;
        dir_sector = next_s;
    }

    return -1;
}

int d64_file_read_byte(d64_file_handle_t *handle) {
    if (!handle || !handle->active || handle->eof) return -1;

    uint8_t *sec = sector_ptr(handle->cur_track, handle->cur_sector);
    if (!sec) {
        handle->eof = true;
        return -1;
    }

    uint8_t next_track = sec[0];
    uint8_t next_sector = sec[1];

    /* Determine end of data in this block */
    uint8_t last_pos;
    if (next_track == 0) {
        /* Last block: next_sector = number of bytes used + 1 */
        last_pos = next_sector;
    } else {
        last_pos = 255;
    }

    if (handle->buf_pos > last_pos) {
        /* Done with this block */
        if (next_track == 0) {
            handle->eof = true;
            return -1;
        }
        /* Move to next block */
        handle->cur_track = next_track;
        handle->cur_sector = next_sector;
        handle->buf_pos = 2;

        sec = sector_ptr(handle->cur_track, handle->cur_sector);
        if (!sec) {
            handle->eof = true;
            return -1;
        }
        next_track = sec[0];
        next_sector = sec[1];
        if (next_track == 0) {
            last_pos = next_sector;
        } else {
            last_pos = 255;
        }

        if (handle->buf_pos > last_pos) {
            handle->eof = true;
            return -1;
        }
    }

    uint8_t byte = sec[handle->buf_pos];
    handle->buf_pos++;

    /* Check if we just read the last byte */
    if (next_track == 0 && handle->buf_pos > last_pos) {
        handle->eof = true;
    }

    return byte;
}

void d64_file_close(d64_file_handle_t *handle) {
    if (!handle) return;

    if (handle->active && handle->write_mode) {
        /* Flush the last partial block */
        if (handle->write_pos > 2) {
            uint8_t *sec = sector_ptr(handle->cur_track, handle->cur_sector);
            if (sec) {
                memcpy(sec, handle->write_buf, D64_SECTOR_SIZE);
                /* Mark as last block: T=0, S=bytes used (last valid byte index) */
                sec[0] = 0;
                sec[1] = handle->write_pos - 1;
                d64_dirty = true;
            }
        } else if (handle->write_pos == 2) {
            /* Empty last block — still mark as last */
            uint8_t *sec = sector_ptr(handle->cur_track, handle->cur_sector);
            if (sec) {
                sec[0] = 0;
                sec[1] = 1;  /* No data bytes */
                d64_dirty = true;
            }
        }

        /* Update directory entry: set closed flag and block count */
        uint8_t *dir_sec = sector_ptr(handle->dir_track, handle->dir_sector);
        if (dir_sec) {
            uint8_t *raw = &dir_sec[handle->dir_entry_index * D64_DIR_ENTRY_SIZE];
            raw[2] = D64_FILE_PRG | D64_FILE_CLOSED;  /* Mark as properly closed */
            raw[30] = handle->blocks_written & 0xFF;
            raw[31] = (handle->blocks_written >> 8) & 0xFF;
            d64_dirty = true;
        }
    }

    handle->active = false;
}

/*
 * BAM operations for write support.
 */

/* Check if a block is free in the BAM */
static bool bam_is_free(uint8_t track, uint8_t sector) {
    if (track < 1 || track > D64_NUM_TRACKS) return false;
    uint8_t *bam = sector_ptr(D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return false;

    uint8_t offset = 4 * track;  /* BAM entry for this track */
    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    return (bam[offset + byte_idx] & bit) != 0;
}

/* Allocate a block in the BAM */
static bool bam_alloc(uint8_t track, uint8_t sector) {
    if (track < 1 || track > D64_NUM_TRACKS) return false;
    uint8_t *bam = sector_ptr(D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return false;

    uint8_t offset = 4 * track;
    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    if (!(bam[offset + byte_idx] & bit)) return false;  /* Already allocated */

    bam[offset + byte_idx] &= ~bit;
    bam[offset]--;  /* Decrement free sector count */
    d64_dirty = true;
    return true;
}

/* Free a block in the BAM */
static bool bam_free(uint8_t track, uint8_t sector) {
    if (track < 1 || track > D64_NUM_TRACKS) return false;
    uint8_t *bam = sector_ptr(D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return false;

    uint8_t offset = 4 * track;
    uint8_t byte_idx = 1 + (sector / 8);
    uint8_t bit = 1 << (sector % 8);

    if (bam[offset + byte_idx] & bit) return false;  /* Already free */

    bam[offset + byte_idx] |= bit;
    bam[offset]++;  /* Increment free sector count */
    d64_dirty = true;
    return true;
}

/*
 * Find a free block, preferring the given track.
 * Uses standard 1541 allocation strategy: start from preferred track,
 * search outward. Skip track 18 (directory track).
 */
static bool find_free_block(uint8_t preferred_track, uint8_t *out_track, uint8_t *out_sector) {
    /* Try preferred track first, then alternate outward */
    for (int delta = 0; delta < D64_NUM_TRACKS; delta++) {
        for (int dir = 0; dir < 2; dir++) {
            int t;
            if (dir == 0) {
                t = preferred_track + delta;
            } else {
                t = preferred_track - delta;
                if (delta == 0) continue;  /* Don't check same track twice */
            }

            if (t < 1 || t > D64_NUM_TRACKS) continue;
            if (t == D64_DIR_TRACK) continue;  /* Skip directory track */

            for (uint8_t s = 0; s < sectors_per_track[t]; s++) {
                if (bam_is_free(t, s)) {
                    *out_track = t;
                    *out_sector = s;
                    return true;
                }
            }
        }
    }

    return false;
}

/* Find a free block on the directory track (track 18) */
static bool find_free_dir_block(uint8_t *out_sector) {
    for (uint8_t s = 0; s < sectors_per_track[D64_DIR_TRACK]; s++) {
        if (s == D64_BAM_SECTOR) continue;  /* Don't overwrite BAM */
        if (bam_is_free(D64_DIR_TRACK, s)) {
            *out_sector = s;
            return true;
        }
    }
    return false;
}

/* Get total free blocks from BAM (excluding track 18) */
static uint16_t bam_free_blocks(void) {
    uint8_t *bam = sector_ptr(D64_BAM_TRACK, D64_BAM_SECTOR);
    if (!bam) return 0;

    uint16_t total = 0;
    for (uint8_t t = 1; t <= D64_NUM_TRACKS; t++) {
        if (t == D64_DIR_TRACK) continue;
        total += bam[4 * t];
    }
    return total;
}

int d64_file_create(const char *name, d64_file_handle_t *handle) {
    if (!d64_mounted || !handle) return -1;

    memset(handle, 0, sizeof(*handle));

    uint8_t name_len = strlen(name);
    if (name_len > 16) name_len = 16;

    /* Find a free directory entry */
    uint8_t dir_track = D64_DIR_TRACK;
    uint8_t dir_sector = D64_DIR_FIRST_SECTOR;
    bool found_slot = false;

    while (dir_track != 0 && !found_slot) {
        uint8_t *sec = sector_ptr(dir_track, dir_sector);
        if (!sec) return -1;

        for (int i = 0; i < D64_DIR_ENTRIES_PER_SECTOR; i++) {
            uint8_t *raw = &sec[i * D64_DIR_ENTRY_SIZE];
            uint8_t ftype = raw[2];

            if ((ftype & D64_FILE_TYPE_MASK) == D64_FILE_DEL ||
                raw[5] == 0x00) {
                /* Found a free slot — fill it in */

                /* Allocate the first data block */
                uint8_t data_t, data_s;
                if (!find_free_block(dir_track, &data_t, &data_s)) {
                    return -1;  /* Disk full */
                }
                bam_alloc(data_t, data_s);

                /* Initialize directory entry */
                memset(raw, 0, D64_DIR_ENTRY_SIZE);
                raw[2] = D64_FILE_PRG;  /* Not closed yet (bit 7 clear) */
                raw[3] = data_t;
                raw[4] = data_s;

                /* Set filename padded with 0xA0 */
                memset(&raw[5], 0xA0, 16);
                for (int j = 0; j < name_len; j++) {
                    raw[5 + j] = toupper((unsigned char)name[j]);
                }

                /* Initialize write handle */
                handle->active = true;
                handle->write_mode = true;
                handle->cur_track = data_t;
                handle->cur_sector = data_s;
                handle->first_track = data_t;
                handle->first_sector = data_s;
                handle->write_pos = 2;  /* Data starts at byte 2 */
                handle->blocks_written = 1;

                /* Store dir entry location for close */
                handle->dir_track = dir_track;
                handle->dir_sector = dir_sector;
                handle->dir_entry_index = i;

                /* Clear the data block */
                uint8_t *data_sec = sector_ptr(data_t, data_s);
                if (data_sec) {
                    memset(data_sec, 0, D64_SECTOR_SIZE);
                }

                memset(handle->write_buf, 0, D64_SECTOR_SIZE);
                d64_dirty = true;

                found_slot = true;
                break;
            }
        }

        if (!found_slot) {
            /* Follow directory chain or extend it */
            uint8_t next_t = sec[0];
            uint8_t next_s = sec[1];

            if (next_t == 0) {
                /* Extend directory: allocate new dir sector */
                uint8_t new_s;
                if (!find_free_dir_block(&new_s)) {
                    return -1;  /* Directory track full */
                }
                bam_alloc(D64_DIR_TRACK, new_s);

                /* Link current sector to new one */
                sec[0] = D64_DIR_TRACK;
                sec[1] = new_s;

                /* Clear new directory sector */
                uint8_t *new_sec = sector_ptr(D64_DIR_TRACK, new_s);
                if (new_sec) {
                    memset(new_sec, 0, D64_SECTOR_SIZE);
                }

                dir_track = D64_DIR_TRACK;
                dir_sector = new_s;
            } else {
                dir_track = next_t;
                dir_sector = next_s;
            }
        }
    }

    if (!found_slot) return -1;
    return 0;
}

int d64_file_write_byte(d64_file_handle_t *handle, uint8_t byte) {
    if (!handle || !handle->active || !handle->write_mode) return -1;

    handle->write_buf[handle->write_pos++] = byte;

    if (handle->write_pos >= D64_SECTOR_SIZE) {
        /* Block is full — write it and allocate next */
        uint8_t next_t, next_s;
        if (!find_free_block(handle->cur_track, &next_t, &next_s)) {
            return -1;  /* Disk full */
        }
        bam_alloc(next_t, next_s);

        /* Set chain pointer to next block */
        handle->write_buf[0] = next_t;
        handle->write_buf[1] = next_s;

        /* Write current block to image */
        uint8_t *sec = sector_ptr(handle->cur_track, handle->cur_sector);
        if (sec) {
            memcpy(sec, handle->write_buf, D64_SECTOR_SIZE);
        }

        /* Move to next block */
        handle->cur_track = next_t;
        handle->cur_sector = next_s;
        handle->write_pos = 2;
        handle->blocks_written++;
        memset(handle->write_buf, 0, D64_SECTOR_SIZE);
        d64_dirty = true;
    }

    return 0;
}

int d64_file_delete(const char *name) {
    if (!d64_mounted) return -1;

    uint8_t name_len = strlen(name);
    uint8_t dir_track = D64_DIR_TRACK;
    uint8_t dir_sector = D64_DIR_FIRST_SECTOR;

    while (dir_track != 0) {
        uint8_t *sec = sector_ptr(dir_track, dir_sector);
        if (!sec) return -1;

        for (int i = 0; i < D64_DIR_ENTRIES_PER_SECTOR; i++) {
            uint8_t *raw = &sec[i * D64_DIR_ENTRY_SIZE];
            uint8_t ftype = raw[2];

            if ((ftype & D64_FILE_TYPE_MASK) == D64_FILE_DEL) continue;
            if (!(ftype & D64_FILE_CLOSED)) continue;

            if (name_match(&raw[5], name, name_len)) {
                /* Free all blocks in the chain */
                uint8_t t = raw[3];
                uint8_t s = raw[4];

                while (t != 0) {
                    uint8_t *block = sector_ptr(t, s);
                    if (!block) break;
                    uint8_t next_t = block[0];
                    uint8_t next_s = block[1];
                    bam_free(t, s);
                    t = next_t;
                    s = next_s;
                }

                /* Mark directory entry as deleted */
                raw[2] = D64_FILE_DEL;
                d64_dirty = true;

                return 0;
            }
        }

        uint8_t next_t = sec[0];
        uint8_t next_s = sec[1];
        if (next_t == 0) break;
        dir_track = next_t;
        dir_sector = next_s;
    }

    return -1;
}
