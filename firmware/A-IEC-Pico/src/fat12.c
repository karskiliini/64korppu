#include "fat12.h"
#include "floppy_ctrl.h"
#include <string.h>
#include <ctype.h>

/* Global filesystem state */
static fat12_state_t fs = {0};

/* Open file handles */
static fat12_file_t open_files[FAT12_MAX_OPEN_FILES] = {0};

/* Sector I/O buffer */
static uint8_t io_buf[FAT12_BYTES_PER_SECTOR];

/* External disk I/O functions (implemented in main.c, delegate to core 1) */
extern int disk_read_sector(uint16_t lba, uint8_t *buf);
extern int disk_write_sector(uint16_t lba, const uint8_t *buf);

int fat12_mount(void) {
    /* Read boot sector (LBA 0) */
    int rc = disk_read_sector(0, io_buf);
    if (rc != 0) return FAT12_ERR_IO;

    /* Validate BPB */
    fat12_bpb_t *bpb = (fat12_bpb_t *)io_buf;

    /* Check basic parameters match 1.44MB floppy */
    if (bpb->bytes_per_sector != FAT12_BYTES_PER_SECTOR ||
        bpb->sectors_per_cluster != FAT12_SECTORS_PER_CLUSTER ||
        bpb->num_fats != FAT12_NUM_FATS ||
        bpb->total_sectors != FAT12_TOTAL_SECTORS) {
        return FAT12_ERR_NOT_FAT12;
    }

    /* Check media type (0xF0 = 1.44MB floppy) */
    if (bpb->media_type != FAT12_MEDIA_BYTE) {
        return FAT12_ERR_NOT_FAT12;
    }

    /* Read volume label and serial */
    memcpy(fs.volume_label, bpb->volume_label, 11);
    fs.volume_label[11] = '\0';
    fs.volume_serial = bpb->volume_serial;

    /* Read FAT into cache */
    for (uint16_t i = 0; i < FAT12_SECTORS_PER_FAT; i++) {
        rc = disk_read_sector(FAT12_FAT1_START + i,
                               &fs.fat_cache[i * FAT12_BYTES_PER_SECTOR]);
        if (rc != 0) return FAT12_ERR_IO;
    }

    /* Verify FAT starts with media byte */
    if (fs.fat_cache[0] != FAT12_MEDIA_BYTE) {
        return FAT12_ERR_NOT_FAT12;
    }

    fs.fat_dirty = false;
    fs.mounted = true;

    return FAT12_OK;
}

void fat12_unmount(void) {
    if (!fs.mounted) return;

    /* Close all open files */
    for (int i = 0; i < FAT12_MAX_OPEN_FILES; i++) {
        if (open_files[i].active) {
            fat12_close(&open_files[i]);
        }
    }

    /* Flush FAT if dirty */
    if (fs.fat_dirty) {
        fat12_flush_fat();
    }

    fs.mounted = false;
}

uint16_t fat12_read_fat_entry(uint16_t cluster) {
    /*
     * FAT12 packs two 12-bit entries into 3 bytes:
     *   Byte offset = cluster * 3 / 2
     *
     * If cluster is even: entry = (byte[offset+1] & 0x0F) << 8 | byte[offset]
     * If cluster is odd:  entry = byte[offset+1] << 4 | (byte[offset] >> 4)
     */
    uint16_t offset = cluster + (cluster / 2);  /* cluster * 1.5 */

    uint16_t entry;
    if (cluster & 1) {
        /* Odd cluster */
        entry = (fs.fat_cache[offset] >> 4) | ((uint16_t)fs.fat_cache[offset + 1] << 4);
    } else {
        /* Even cluster */
        entry = fs.fat_cache[offset] | (((uint16_t)fs.fat_cache[offset + 1] & 0x0F) << 8);
    }

    return entry;
}

void fat12_write_fat_entry(uint16_t cluster, uint16_t value) {
    uint16_t offset = cluster + (cluster / 2);
    value &= 0xFFF;

    if (cluster & 1) {
        /* Odd cluster */
        fs.fat_cache[offset] = (fs.fat_cache[offset] & 0x0F) | ((value & 0x0F) << 4);
        fs.fat_cache[offset + 1] = (value >> 4) & 0xFF;
    } else {
        /* Even cluster */
        fs.fat_cache[offset] = value & 0xFF;
        fs.fat_cache[offset + 1] = (fs.fat_cache[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }

    fs.fat_dirty = true;
}

int fat12_flush_fat(void) {
    if (!fs.fat_dirty) return FAT12_OK;

    /* Write both FAT copies */
    for (uint16_t i = 0; i < FAT12_SECTORS_PER_FAT; i++) {
        int rc = disk_write_sector(FAT12_FAT1_START + i,
                                    &fs.fat_cache[i * FAT12_BYTES_PER_SECTOR]);
        if (rc != 0) return FAT12_ERR_IO;

        rc = disk_write_sector(FAT12_FAT2_START + i,
                                &fs.fat_cache[i * FAT12_BYTES_PER_SECTOR]);
        if (rc != 0) return FAT12_ERR_IO;
    }

    fs.fat_dirty = false;
    return FAT12_OK;
}

/*
 * Convert cluster number to LBA sector number.
 */
static uint16_t cluster_to_lba(uint16_t cluster) {
    return FAT12_DATA_START + (cluster - 2) * FAT12_SECTORS_PER_CLUSTER;
}

/*
 * Find a free cluster in the FAT.
 * Returns cluster number or 0 if disk full.
 */
static uint16_t find_free_cluster(void) {
    for (uint16_t c = 2; c < FAT12_TOTAL_CLUSTERS + 2; c++) {
        if (fat12_read_fat_entry(c) == FAT12_FREE) {
            return c;
        }
    }
    return 0;  /* Disk full */
}

int fat12_find_file(const char *name, const char *ext, fat12_dirent_t *entry) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    for (uint16_t sector = 0; sector < FAT12_ROOT_DIR_SECTORS; sector++) {
        int rc = disk_read_sector(FAT12_ROOT_DIR_START + sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)io_buf;
        uint16_t count = FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t);

        for (uint16_t i = 0; i < count; i++) {
            /* End of directory */
            if (entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;

            /* Deleted entry */
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;

            /* Skip volume labels and subdirectories */
            if (entries[i].attr & (FAT12_ATTR_VOLUME_ID | FAT12_ATTR_DIRECTORY)) continue;

            /* Compare name and extension */
            if (memcmp(entries[i].name, name, 8) == 0 &&
                memcmp(entries[i].ext, ext, 3) == 0) {
                if (entry) *entry = entries[i];
                return FAT12_OK;
            }
        }
    }

    return FAT12_ERR_NOT_FOUND;
}

int fat12_open_read(const char *name, const char *ext, fat12_file_t *file) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    fat12_dirent_t entry;
    int rc = fat12_find_file(name, ext, &entry);
    if (rc != FAT12_OK) return rc;

    file->active = true;
    file->first_cluster = entry.cluster_lo;
    file->current_cluster = entry.cluster_lo;
    file->file_size = entry.file_size;
    file->position = 0;
    file->write_mode = false;
    file->modified = false;
    file->eof = false;

    return FAT12_OK;
}

int fat12_read(fat12_file_t *file, uint8_t *buf, uint16_t count) {
    if (!file->active) return FAT12_ERR_INVALID;

    uint16_t bytes_read = 0;

    while (bytes_read < count && file->position < file->file_size) {
        /* Calculate position within current cluster */
        uint16_t cluster_offset = file->position % FAT12_BYTES_PER_SECTOR;

        /* Read the current cluster's sector */
        uint16_t lba = cluster_to_lba(file->current_cluster);
        int rc = disk_read_sector(lba, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        /* Copy data from sector */
        uint16_t remaining_in_sector = FAT12_BYTES_PER_SECTOR - cluster_offset;
        uint16_t remaining_in_file = file->file_size - file->position;
        uint16_t remaining_requested = count - bytes_read;
        uint16_t to_copy = remaining_in_sector;
        if (to_copy > remaining_in_file) to_copy = remaining_in_file;
        if (to_copy > remaining_requested) to_copy = remaining_requested;

        memcpy(&buf[bytes_read], &io_buf[cluster_offset], to_copy);
        bytes_read += to_copy;
        file->position += to_copy;

        /* Move to next cluster if we've consumed this one */
        if (file->position % FAT12_BYTES_PER_SECTOR == 0 &&
            file->position < file->file_size) {
            uint16_t next = fat12_read_fat_entry(file->current_cluster);
            if (next >= FAT12_EOC_MIN) {
                file->eof = true;
                break;  /* End of chain */
            }
            file->current_cluster = next;
        }
    }

    if (file->position >= file->file_size) {
        file->eof = true;
    }

    return bytes_read;
}

int fat12_create(const char *name, const char *ext, fat12_file_t *file) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    /* Check if file already exists */
    fat12_dirent_t existing;
    if (fat12_find_file(name, ext, &existing) == FAT12_OK) {
        /* Delete existing file first (overwrite behavior) */
        fat12_delete(name, ext);
    }

    /* Find a free directory entry */
    for (uint16_t sector = 0; sector < FAT12_ROOT_DIR_SECTORS; sector++) {
        int rc = disk_read_sector(FAT12_ROOT_DIR_START + sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)io_buf;
        uint16_t count = FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t);

        for (uint16_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00 || (uint8_t)entries[i].name[0] == 0xE5) {
                /* Found a free entry */
                memset(&entries[i], 0, sizeof(fat12_dirent_t));
                memcpy(entries[i].name, name, 8);
                memcpy(entries[i].ext, ext, 3);
                entries[i].attr = FAT12_ATTR_ARCHIVE;
                entries[i].cluster_lo = 0;  /* No clusters yet */
                entries[i].file_size = 0;

                /* Write directory sector back */
                rc = disk_write_sector(FAT12_ROOT_DIR_START + sector, io_buf);
                if (rc != 0) return FAT12_ERR_IO;

                /* Initialize file handle */
                file->active = true;
                file->first_cluster = 0;
                file->current_cluster = 0;
                file->file_size = 0;
                file->position = 0;
                file->dir_entry_sector = FAT12_ROOT_DIR_START + sector;
                file->dir_entry_offset = i * sizeof(fat12_dirent_t);
                file->write_mode = true;
                file->modified = false;

                return FAT12_OK;
            }
        }
    }

    return FAT12_ERR_DIR_FULL;
}

int fat12_write(fat12_file_t *file, const uint8_t *buf, uint16_t count) {
    if (!file->active || !file->write_mode) return FAT12_ERR_INVALID;

    uint16_t bytes_written = 0;

    while (bytes_written < count) {
        /* Allocate a cluster if needed */
        if (file->current_cluster == 0 || file->position % FAT12_BYTES_PER_SECTOR == 0) {
            if (file->position > 0 || file->current_cluster == 0) {
                uint16_t new_cluster = find_free_cluster();
                if (new_cluster == 0) return FAT12_ERR_DISK_FULL;

                fat12_write_fat_entry(new_cluster, 0xFFF);  /* Mark as end of chain */

                if (file->current_cluster != 0) {
                    /* Link previous cluster to new one */
                    fat12_write_fat_entry(file->current_cluster, new_cluster);
                } else {
                    /* First cluster */
                    file->first_cluster = new_cluster;
                }

                file->current_cluster = new_cluster;
            }
        }

        /* Write data to current cluster */
        uint16_t cluster_offset = file->position % FAT12_BYTES_PER_SECTOR;
        uint16_t space_in_sector = FAT12_BYTES_PER_SECTOR - cluster_offset;
        uint16_t to_write = count - bytes_written;
        if (to_write > space_in_sector) to_write = space_in_sector;

        /* Read-modify-write if partial sector */
        uint16_t lba = cluster_to_lba(file->current_cluster);
        if (cluster_offset > 0 || to_write < FAT12_BYTES_PER_SECTOR) {
            disk_read_sector(lba, io_buf);
        }

        memcpy(&io_buf[cluster_offset], &buf[bytes_written], to_write);

        int rc = disk_write_sector(lba, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        bytes_written += to_write;
        file->position += to_write;
        file->file_size = file->position;  /* Extend file */
        file->modified = true;
    }

    return bytes_written;
}

int fat12_close(fat12_file_t *file) {
    if (!file->active) return FAT12_OK;

    if (file->write_mode && file->modified) {
        /* Update directory entry with final file size and first cluster */
        int rc = disk_read_sector(file->dir_entry_sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entry = (fat12_dirent_t *)&io_buf[file->dir_entry_offset];
        entry->file_size = file->file_size;
        entry->cluster_lo = file->first_cluster;

        rc = disk_write_sector(file->dir_entry_sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        /* Flush FAT */
        rc = fat12_flush_fat();
        if (rc != FAT12_OK) return rc;
    }

    file->active = false;
    return FAT12_OK;
}

int fat12_delete(const char *name, const char *ext) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    for (uint16_t sector = 0; sector < FAT12_ROOT_DIR_SECTORS; sector++) {
        int rc = disk_read_sector(FAT12_ROOT_DIR_START + sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)io_buf;
        uint16_t count = FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t);

        for (uint16_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;
            if (entries[i].attr & (FAT12_ATTR_VOLUME_ID | FAT12_ATTR_DIRECTORY)) continue;

            if (memcmp(entries[i].name, name, 8) == 0 &&
                memcmp(entries[i].ext, ext, 3) == 0) {
                /* Free cluster chain */
                uint16_t cluster = entries[i].cluster_lo;
                while (cluster >= 2 && cluster < FAT12_EOC_MIN) {
                    uint16_t next = fat12_read_fat_entry(cluster);
                    fat12_write_fat_entry(cluster, FAT12_FREE);
                    cluster = next;
                }

                /* Mark directory entry as deleted */
                entries[i].name[0] = 0xE5;
                rc = disk_write_sector(FAT12_ROOT_DIR_START + sector, io_buf);
                if (rc != 0) return FAT12_ERR_IO;

                return fat12_flush_fat();
            }
        }
    }

    return FAT12_ERR_NOT_FOUND;
}

int fat12_rename(const char *old_name, const char *old_ext,
                  const char *new_name, const char *new_ext) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    /* Check new name doesn't exist */
    fat12_dirent_t tmp;
    if (fat12_find_file(new_name, new_ext, &tmp) == FAT12_OK) {
        return FAT12_ERR_EXISTS;
    }

    /* Find and rename */
    for (uint16_t sector = 0; sector < FAT12_ROOT_DIR_SECTORS; sector++) {
        int rc = disk_read_sector(FAT12_ROOT_DIR_START + sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)io_buf;
        uint16_t count = FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t);

        for (uint16_t i = 0; i < count; i++) {
            if (entries[i].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;
            if ((uint8_t)entries[i].name[0] == 0xE5) continue;

            if (memcmp(entries[i].name, old_name, 8) == 0 &&
                memcmp(entries[i].ext, old_ext, 3) == 0) {
                memcpy(entries[i].name, new_name, 8);
                memcpy(entries[i].ext, new_ext, 3);

                return disk_write_sector(FAT12_ROOT_DIR_START + sector, io_buf);
            }
        }
    }

    return FAT12_ERR_NOT_FOUND;
}

int fat12_readdir(uint16_t *index, fat12_dirent_t *entry) {
    if (!fs.mounted) return FAT12_ERR_NOT_MOUNT;

    while (*index < FAT12_ROOT_ENTRIES) {
        uint16_t sector = *index / (FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t));
        uint16_t offset = *index % (FAT12_BYTES_PER_SECTOR / sizeof(fat12_dirent_t));

        int rc = disk_read_sector(FAT12_ROOT_DIR_START + sector, io_buf);
        if (rc != 0) return FAT12_ERR_IO;

        fat12_dirent_t *entries = (fat12_dirent_t *)io_buf;

        (*index)++;

        /* End of directory */
        if (entries[offset].name[0] == 0x00) return FAT12_ERR_NOT_FOUND;

        /* Skip deleted entries */
        if ((uint8_t)entries[offset].name[0] == 0xE5) continue;

        /* Skip LFN entries */
        if (entries[offset].attr == FAT12_ATTR_LFN) continue;

        *entry = entries[offset];
        return FAT12_OK;
    }

    return FAT12_ERR_NOT_FOUND;
}

uint32_t fat12_free_space(void) {
    if (!fs.mounted) return 0;

    uint32_t free_clusters = 0;
    for (uint16_t c = 2; c < FAT12_TOTAL_CLUSTERS + 2; c++) {
        if (fat12_read_fat_entry(c) == FAT12_FREE) {
            free_clusters++;
        }
    }

    return free_clusters * FAT12_SECTORS_PER_CLUSTER * FAT12_BYTES_PER_SECTOR;
}

int fat12_format(const char *label) {
    /* Write boot sector */
    memset(io_buf, 0, FAT12_BYTES_PER_SECTOR);

    fat12_bpb_t *bpb = (fat12_bpb_t *)io_buf;
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;
    memcpy(bpb->oem_name, "64KORPPU", 8);
    bpb->bytes_per_sector = FAT12_BYTES_PER_SECTOR;
    bpb->sectors_per_cluster = FAT12_SECTORS_PER_CLUSTER;
    bpb->reserved_sectors = FAT12_RESERVED_SECTORS;
    bpb->num_fats = FAT12_NUM_FATS;
    bpb->root_entries = FAT12_ROOT_ENTRIES;
    bpb->total_sectors = FAT12_TOTAL_SECTORS;
    bpb->media_type = FAT12_MEDIA_BYTE;
    bpb->sectors_per_fat = FAT12_SECTORS_PER_FAT;
    bpb->sectors_per_track = 18;
    bpb->num_heads = 2;
    bpb->drive_number = 0x00;
    bpb->boot_sig = 0x29;
    bpb->volume_serial = 0x64C64FAT;  /* Fun serial number */

    if (label) {
        memcpy(bpb->volume_label, label, 11);
    } else {
        memcpy(bpb->volume_label, "64KORPPU   ", 11);
    }
    memcpy(bpb->fs_type, "FAT12   ", 8);

    /* Boot signature at end of sector */
    io_buf[510] = 0x55;
    io_buf[511] = 0xAA;

    int rc = disk_write_sector(0, io_buf);
    if (rc != 0) return FAT12_ERR_IO;

    /* Initialize FAT (both copies) */
    memset(io_buf, 0, FAT12_BYTES_PER_SECTOR);

    /* First FAT sector: media byte + 0xFFF for cluster 0 and 1 */
    io_buf[0] = FAT12_MEDIA_BYTE;
    io_buf[1] = 0xFF;
    io_buf[2] = 0xFF;

    rc = disk_write_sector(FAT12_FAT1_START, io_buf);
    if (rc != 0) return FAT12_ERR_IO;
    rc = disk_write_sector(FAT12_FAT2_START, io_buf);
    if (rc != 0) return FAT12_ERR_IO;

    /* Rest of FAT sectors: all zeros */
    memset(io_buf, 0, FAT12_BYTES_PER_SECTOR);
    for (uint16_t i = 1; i < FAT12_SECTORS_PER_FAT; i++) {
        disk_write_sector(FAT12_FAT1_START + i, io_buf);
        disk_write_sector(FAT12_FAT2_START + i, io_buf);
    }

    /* Clear root directory */
    memset(io_buf, 0, FAT12_BYTES_PER_SECTOR);
    for (uint16_t i = 0; i < FAT12_ROOT_DIR_SECTORS; i++) {
        disk_write_sector(FAT12_ROOT_DIR_START + i, io_buf);
    }

    /* Create volume label entry in root directory */
    if (label) {
        fat12_dirent_t *vol = (fat12_dirent_t *)io_buf;
        memcpy(vol->name, label, 8);
        memcpy(vol->ext, label + 8, 3);
        vol->attr = FAT12_ATTR_VOLUME_ID;
        disk_write_sector(FAT12_ROOT_DIR_START, io_buf);
    }

    /* Re-mount the freshly formatted disk */
    return fat12_mount();
}

void fat12_parse_filename(const char *cbm_name, char *name8, char *ext3) {
    /* Initialize with spaces */
    memset(name8, ' ', 8);
    memset(ext3, ' ', 3);

    /* Default extension for C64 programs */
    memcpy(ext3, "PRG", 3);

    if (!cbm_name || !cbm_name[0]) return;

    /* Find dot separator */
    const char *dot = NULL;
    for (const char *p = cbm_name; *p; p++) {
        if (*p == '.') {
            dot = p;
            break;
        }
    }

    /* Copy name part (before dot or full string) */
    int len = dot ? (dot - cbm_name) : strlen(cbm_name);
    if (len > 8) len = 8;
    for (int i = 0; i < len; i++) {
        name8[i] = toupper((unsigned char)cbm_name[i]);
    }

    /* Copy extension if present */
    if (dot && dot[1]) {
        int elen = strlen(dot + 1);
        if (elen > 3) elen = 3;
        for (int i = 0; i < elen; i++) {
            ext3[i] = toupper((unsigned char)dot[i + 1]);
        }
    }
}
