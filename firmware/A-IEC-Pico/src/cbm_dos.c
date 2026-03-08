#include "cbm_dos.h"
#include "iec_protocol.h"
#include "fat12.h"
#include "d64.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/*
 * CBM-DOS emulation layer.
 *
 * Maps Commodore disk operations to FAT12 filesystem:
 *
 * LOAD "FILENAME",8     → Opens FILENAME.PRG, reads 2-byte header + data
 * SAVE "FILENAME",8     → Creates FILENAME.PRG, writes 2-byte header + data
 * LOAD "$",8            → Generates CBM-format directory listing
 * OPEN 15,8,15,"cmd"    → Executes command on channel 15
 *
 * Directory listing format (as BASIC program):
 *   0 "DISK LABEL      " ID FAT12
 *   123  "FILENAME PRG"   PRG
 *   456  BLOCKS FREE.
 *
 * Each "block" = 256 bytes (to match CBM convention)
 */

/* D64 mode state */
static bool d64_mode = false;
static d64_file_handle_t d64_handle;

/* File handles for open channels */
static fat12_file_t channel_files[IEC_NUM_CHANNELS];

/* Directory listing buffer (generated on LOAD "$") */
static uint8_t dir_buffer[8192];  /* Max ~8KB for directory listing */
static uint16_t dir_len = 0;
static uint16_t dir_pos = 0;
static bool dir_active = false;

void cbm_dos_init(void) {
    memset(channel_files, 0, sizeof(channel_files));
    memset(&d64_handle, 0, sizeof(d64_handle));
    d64_mode = false;
    dir_active = false;

    /* Set initial status: "73, 64KORPPU V1.0,00,00" */
    iec_set_error(CBM_ERR_DOS_MISMATCH, CBM_DOS_ID, 0, 0);
}

int cbm_dos_format_error(uint8_t code, const char *msg,
                          uint8_t track, uint8_t sector,
                          char *buf, uint8_t buflen) {
    return snprintf(buf, buflen, "%02d, %s,%02d,%02d\r",
                     code, msg, track, sector);
}

/*
 * Generate a CBM-style directory listing as a BASIC program.
 *
 * The listing is formatted as a tokenized BASIC program that can be
 * loaded and listed with the LIST command. Each line:
 *   - 2 bytes: pointer to next line (or 0x0000 for end)
 *   - 2 bytes: line number (= block count for files, 0 for header)
 *   - Variable: BASIC tokens / text
 *   - 1 byte: 0x00 line terminator
 */
/*
 * Helper: write BASIC directory header line.
 */
static uint16_t dir_write_header(uint8_t *buf, uint16_t pos,
                                  const char *label, const char *id) {
    uint16_t line_start = pos;
    buf[pos++] = 0x00;  /* Next line pointer (placeholder) */
    buf[pos++] = 0x00;

    /* Line number = 0 */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    buf[pos++] = 0x12;  /* RVS ON */
    buf[pos++] = '"';

    for (int i = 0; i < 16 && label[i]; i++) {
        buf[pos++] = label[i];
    }
    buf[pos++] = '"';
    buf[pos++] = ' ';

    for (const char *p = id; *p; p++) {
        buf[pos++] = *p;
    }

    buf[pos++] = 0x00;  /* End of line */

    /* Fix next line pointer */
    buf[line_start] = (pos + 0x0401) & 0xFF;
    buf[line_start + 1] = ((pos + 0x0401) >> 8) & 0xFF;

    return pos;
}

/*
 * Helper: write a file entry line in BASIC directory format.
 */
static uint16_t dir_write_entry(uint8_t *buf, uint16_t pos,
                                 uint16_t blocks, const char *name,
                                 int name_len, const char *type) {
    uint16_t line_start = pos;
    buf[pos++] = 0x00;  /* Next line pointer (placeholder) */
    buf[pos++] = 0x00;

    buf[pos++] = blocks & 0xFF;
    buf[pos++] = (blocks >> 8) & 0xFF;

    /* Spacing for alignment */
    if (blocks < 10) buf[pos++] = ' ';
    if (blocks < 100) buf[pos++] = ' ';
    if (blocks < 1000) buf[pos++] = ' ';

    buf[pos++] = '"';
    for (int i = 0; i < name_len; i++) {
        buf[pos++] = name[i];
    }
    buf[pos++] = '"';

    /* Pad to column for type */
    for (int i = name_len; i < 16; i++) {
        buf[pos++] = ' ';
    }

    for (const char *p = type; *p; p++) {
        buf[pos++] = *p;
    }

    buf[pos++] = 0x00;

    /* Fix next line pointer */
    buf[line_start] = (pos + 0x0401) & 0xFF;
    buf[line_start + 1] = ((pos + 0x0401) >> 8) & 0xFF;

    return pos;
}

/*
 * Generate D64 directory listing in BASIC format.
 */
static void cbm_dos_generate_d64_directory(void) {
    uint16_t pos = 0;
    uint8_t *buf = dir_buffer;

    /* Load address: $0401 */
    buf[pos++] = 0x01;
    buf[pos++] = 0x04;

    /* Header with D64 disk name */
    char disk_name[17];
    d64_get_disk_name(disk_name);

    /* Pad disk name to 16 chars */
    char padded_name[17];
    memset(padded_name, ' ', 16);
    padded_name[16] = '\0';
    int nlen = strlen(disk_name);
    if (nlen > 16) nlen = 16;
    memcpy(padded_name, disk_name, nlen);

    pos = dir_write_header(buf, pos, padded_name, "64 D64");

    /* File entries from D64 */
    d64_dir_entry_t entry;
    int rc = d64_dir_first(&entry);
    while (rc == 0) {
        int fname_len = strlen(entry.filename);

        const char *type_str;
        switch (entry.file_type) {
            case D64_FILE_PRG: type_str = "PRG"; break;
            case D64_FILE_SEQ: type_str = "SEQ"; break;
            case D64_FILE_USR: type_str = "USR"; break;
            case D64_FILE_REL: type_str = "REL"; break;
            default:           type_str = "PRG"; break;
        }

        pos = dir_write_entry(buf, pos, entry.size_blocks,
                              entry.filename, fname_len, type_str);

        if (pos > sizeof(dir_buffer) - 64) break;

        rc = d64_dir_next(&entry);
    }

    /* "Blocks free" line — count free blocks from BAM */
    uint16_t line_start = pos;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    /* Read free blocks from BAM: sum track free counts, skip track 18 */
    uint8_t bam_sector[256];
    uint16_t free_blocks = 0;
    if (d64_read_sector(D64_BAM_TRACK, D64_BAM_SECTOR, bam_sector)) {
        for (uint8_t t = 1; t <= D64_NUM_TRACKS; t++) {
            if (t == D64_DIR_TRACK) continue;
            free_blocks += bam_sector[4 * t];
        }
    }

    buf[pos++] = free_blocks & 0xFF;
    buf[pos++] = (free_blocks >> 8) & 0xFF;

    const char *free_msg = "BLOCKS FREE.";
    for (const char *p = free_msg; *p; p++) {
        buf[pos++] = *p;
    }
    buf[pos++] = 0x00;

    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    dir_len = pos;
    dir_pos = 0;
    dir_active = true;
}

void cbm_dos_generate_directory(iec_channel_t *channel) {
    if (d64_mode) {
        cbm_dos_generate_d64_directory();
        return;
    }

    uint16_t pos = 0;
    uint8_t *buf = dir_buffer;

    /* Load address: $0401 (BASIC start) */
    buf[pos++] = 0x01;
    buf[pos++] = 0x04;

    /* Header line */
    pos = dir_write_header(buf, pos, "64KORPPU        ", "64 FAT");

    /* File entries */
    uint16_t dir_index = 0;
    fat12_dirent_t entry;

    while (fat12_readdir(&dir_index, &entry) == FAT12_OK) {
        /* Skip volume labels */
        if (entry.attr & FAT12_ATTR_VOLUME_ID) continue;

        /* Line number = blocks (file_size / 256, rounded up, min 1) */
        uint16_t blocks = (entry.file_size + 255) / 256;
        if (blocks == 0) blocks = 1;

        /* Build filename string */
        char fname[20];
        int fname_len = 0;

        /* Filename (8 chars, trim trailing spaces) */
        int name_len = 8;
        while (name_len > 0 && entry.name[name_len - 1] == ' ') name_len--;
        for (int i = 0; i < name_len; i++) {
            fname[fname_len++] = entry.name[i];
        }

        /* Add dot and extension if not spaces */
        int ext_len = 3;
        while (ext_len > 0 && entry.ext[ext_len - 1] == ' ') ext_len--;
        if (ext_len > 0) {
            fname[fname_len++] = '.';
            for (int i = 0; i < ext_len; i++) {
                fname[fname_len++] = entry.ext[i];
            }
        }

        /* File type */
        const char *type_str;
        if (entry.attr & FAT12_ATTR_DIRECTORY) {
            type_str = "DIR";
        } else if (ext_len >= 3 && memcmp(entry.ext, "PRG", 3) == 0) {
            type_str = "PRG";
        } else if (ext_len >= 3 && memcmp(entry.ext, "SEQ", 3) == 0) {
            type_str = "SEQ";
        } else {
            type_str = "PRG";
        }

        pos = dir_write_entry(buf, pos, blocks, fname, fname_len, type_str);

        if (pos > sizeof(dir_buffer) - 64) break;  /* Safety margin */
    }

    /* "Blocks free" line */
    uint16_t line_start = pos;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    uint32_t free_bytes = fat12_free_space();
    uint16_t free_blocks = free_bytes / 256;
    buf[pos++] = free_blocks & 0xFF;
    buf[pos++] = (free_blocks >> 8) & 0xFF;

    const char *free_msg = "BLOCKS FREE.";
    for (const char *p = free_msg; *p; p++) {
        buf[pos++] = *p;
    }
    buf[pos++] = 0x00;

    /* End of program: two zero bytes */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;

    dir_len = pos;
    dir_pos = 0;
    dir_active = true;
}

void cbm_dos_open(uint8_t sa, const char *filename, uint8_t len) {
    char name8[9] = {0};
    char ext3[4] = {0};

    if (sa == IEC_SA_LOAD || sa == IEC_SA_SAVE) {
        /* LOAD/SAVE: filename comes with the command */

        /* Check for directory listing */
        if (sa == IEC_SA_LOAD && len == 1 && filename[0] == '$') {
            cbm_dos_generate_directory(NULL);
            iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            return;
        }

        if (d64_mode) {
            /* D64 mode: operate on files within the mounted D64 image */
            if (sa == IEC_SA_LOAD) {
                int rc = d64_file_open(filename, &d64_handle);
                if (rc != 0) {
                    iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    return;
                }
            } else {
                int rc = d64_file_create(filename, &d64_handle);
                if (rc != 0) {
                    iec_set_error(CBM_ERR_DISK_FULL, "DISK FULL", 0, 0);
                    return;
                }
            }
            iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            return;
        }

        /* FAT12 mode */
        fat12_parse_filename(filename, name8, ext3);

        if (sa == IEC_SA_LOAD) {
            /* Open for reading */
            int rc = fat12_open_read(name8, ext3, &channel_files[sa]);
            if (rc != FAT12_OK) {
                iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                return;
            }
        } else {
            /* Open for writing (SAVE) */
            int rc = fat12_create(name8, ext3, &channel_files[sa]);
            if (rc != FAT12_OK) {
                if (rc == FAT12_ERR_DISK_FULL) {
                    iec_set_error(CBM_ERR_DISK_FULL, "DISK FULL", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
                return;
            }
        }

        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
    }
}

void cbm_dos_close(uint8_t sa) {
    if (d64_mode && d64_handle.active) {
        d64_file_close(&d64_handle);
    } else if (channel_files[sa].active) {
        fat12_close(&channel_files[sa]);
    }
    dir_active = false;
}

bool cbm_dos_talk_byte(uint8_t sa, uint8_t *byte, bool *eoi) {
    *eoi = false;

    if (sa == IEC_SA_COMMAND) {
        /* Error channel: read status message */
        iec_channel_t *ch = &((iec_device_t *)0)->channels[sa]; /* TODO: fix access */
        /* For now, handled in iec_protocol directly */
        return false;
    }

    if (sa == IEC_SA_LOAD) {
        /* Check for directory listing */
        if (dir_active) {
            if (dir_pos < dir_len) {
                *byte = dir_buffer[dir_pos++];
                if (dir_pos >= dir_len) {
                    *eoi = true;
                    dir_active = false;
                }
                return true;
            }
            *eoi = true;
            return false;
        }

        /* D64 mode: read from D64 file handle */
        if (d64_mode && d64_handle.active) {
            int val = d64_file_read_byte(&d64_handle);
            if (val >= 0) {
                *byte = (uint8_t)val;
                if (d64_handle.eof) {
                    *eoi = true;
                }
                return true;
            }
            *eoi = true;
            return false;
        }

        /* FAT12 mode: read from file */
        if (channel_files[sa].active) {
            uint8_t buf;
            int rc = fat12_read(&channel_files[sa], &buf, 1);
            if (rc == 1) {
                *byte = buf;
                /* Check if next read would be EOF */
                if (channel_files[sa].position >= channel_files[sa].file_size) {
                    *eoi = true;
                }
                return true;
            }
            *eoi = true;
            return false;
        }
    }

    *eoi = true;
    return false;
}

void cbm_dos_listen_byte(uint8_t sa, uint8_t byte) {
    if (d64_mode && sa == IEC_SA_SAVE && d64_handle.active) {
        d64_file_write_byte(&d64_handle, byte);
        return;
    }
    if (sa == IEC_SA_SAVE && channel_files[sa].active) {
        fat12_write(&channel_files[sa], &byte, 1);
    }
}

void cbm_dos_execute_command(const char *cmd, uint8_t len) {
    if (len == 0) return;

    char name8[9], ext3[4];
    char cmd_buf[42];

    /* Copy command to working buffer */
    uint8_t cmd_len = len < sizeof(cmd_buf) - 1 ? len : sizeof(cmd_buf) - 1;
    memcpy(cmd_buf, cmd, cmd_len);
    cmd_buf[cmd_len] = '\0';

    /* Convert to uppercase */
    for (int i = 0; i < cmd_len; i++) {
        cmd_buf[i] = toupper((unsigned char)cmd_buf[i]);
    }

    switch (cmd_buf[0]) {
        case 'C': {
            /* CD:filename - Change directory / mount D64 */
            if (cmd_len > 3 && cmd_buf[1] == 'D' && cmd_buf[2] == ':') {
                const char *arg = &cmd_buf[3];

                if (strcmp(arg, "..") == 0) {
                    /* CD:.. - unmount D64 and return to FAT12 root */
                    if (d64_mode) {
                        d64_unmount();
                        d64_mode = false;
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    } else {
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    }
                } else {
                    /* CD:GAME.D64 - mount a D64 image */
                    if (d64_mode) {
                        /* Already in D64 mode, unmount first */
                        d64_unmount();
                        d64_mode = false;
                    }
                    if (d64_mount(arg)) {
                        d64_mode = true;
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    }
                }
            } else {
                iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            }
            break;
        }

        case 'S': {
            /* S:filename - Scratch (delete) */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                if (d64_mode) {
                    /* Delete file within mounted D64 */
                    int rc = d64_file_delete(&cmd_buf[2]);
                    if (rc == 0) {
                        iec_set_error(CBM_ERR_FILES_SCRATCHED, "FILES SCRATCHED", 1, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    }
                } else {
                    fat12_parse_filename(&cmd_buf[2], name8, ext3);
                    int rc = fat12_delete(name8, ext3);
                    if (rc == FAT12_OK) {
                        iec_set_error(CBM_ERR_FILES_SCRATCHED, "FILES SCRATCHED", 1, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    }
                }
            } else {
                iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            }
            break;
        }

        case 'R': {
            /* R:newname=oldname - Rename */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                /* Find the '=' separator */
                char *eq = strchr(&cmd_buf[2], '=');
                if (eq) {
                    *eq = '\0';
                    char new_name8[9], new_ext3[4];
                    char old_name8[9], old_ext3[4];
                    fat12_parse_filename(&cmd_buf[2], new_name8, new_ext3);
                    fat12_parse_filename(eq + 1, old_name8, old_ext3);

                    int rc = fat12_rename(old_name8, old_ext3, new_name8, new_ext3);
                    if (rc == FAT12_OK) {
                        iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                    } else if (rc == FAT12_ERR_NOT_FOUND) {
                        iec_set_error(CBM_ERR_FILE_NOT_FOUND, "FILE NOT FOUND", 0, 0);
                    } else {
                        iec_set_error(CBM_ERR_FILE_EXISTS, "FILE EXISTS", 0, 0);
                    }
                } else {
                    iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
                }
            }
            break;
        }

        case 'N': {
            /* N:label - Format/New disk */
            if (cmd_len > 2 && cmd_buf[1] == ':') {
                /* Parse volume label (pad to 11 chars) */
                char label[12];
                memset(label, ' ', 11);
                label[11] = '\0';
                int llen = strlen(&cmd_buf[2]);
                if (llen > 11) llen = 11;
                memcpy(label, &cmd_buf[2], llen);

                int rc = fat12_format(label);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
            } else {
                /* N without label */
                int rc = fat12_format(NULL);
                if (rc == FAT12_OK) {
                    iec_set_error(CBM_ERR_OK, "OK", 0, 0);
                } else {
                    iec_set_error(CBM_ERR_WRITE_ERROR, "WRITE ERROR", 0, 0);
                }
            }
            break;
        }

        case 'I': {
            /* I - Initialize (re-read disk) */
            fat12_unmount();
            int rc = fat12_mount();
            if (rc == FAT12_OK) {
                iec_set_error(CBM_ERR_OK, "OK", 0, 0);
            } else {
                iec_set_error(CBM_ERR_DRIVE_NOT_READY, "DRIVE NOT READY", 0, 0);
            }
            break;
        }

        default:
            iec_set_error(CBM_ERR_SYNTAX_ERROR, "SYNTAX ERROR", 0, 0);
            break;
    }
}
