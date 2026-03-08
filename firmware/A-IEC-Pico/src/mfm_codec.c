#include "mfm_codec.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"

/*
 * MFM encoding/decoding for IBM PC floppy format.
 *
 * MFM encoding rules:
 *   Data bit 1: always write flux transition    → clock=0, data=1 → "01"
 *   Data bit 0: write clock if prev data was 0  → clock=1, data=0 → "10"
 *               no clock if prev data was 1     → clock=0, data=0 → "00"
 *
 * Special sync pattern: 0xA1 with missing clock bit
 *   Normal 0xA1 MFM = 0100 0100 1010 1001
 *   Sync   0xA1 MFM = 0100 0100 1000 1001  (bit 5 clock missing)
 *   This pattern cannot appear in normal data stream.
 */

/* MFM encoding table: maps 4 data bits to 8 MFM bits */
/* Index = (prev_bit << 4) | data_nibble */
static const uint8_t mfm_encode_table[32] = {
    /* prev=0: insert clock before 0-bits that follow 0 */
    0xAA, 0xA9, 0xA4, 0xA5, 0x92, 0x91, 0x94, 0x95,  /* 0x0-0x7 */
    0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,  /* 0x8-0xF */
    /* prev=1: no clock before first bit (it follows a 1) */
    0x2A, 0x29, 0x24, 0x25, 0x12, 0x11, 0x14, 0x15,  /* 0x0-0x7 */
    0x4A, 0x49, 0x44, 0x45, 0x52, 0x51, 0x54, 0x55,  /* 0x8-0xF */
};

void mfm_init(void) {
    /*
     * Initialize PIO state machines for MFM read/write.
     *
     * PIO0 SM0: MFM read - measures flux transition timing on /RDATA
     * PIO0 SM1: MFM write - generates flux transitions on /WDATA
     *
     * TODO: Load PIO programs and configure state machines
     */
}

uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc) {
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ MFM_CRC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/*
 * Find the sync pattern (3x 0xA1 with missing clock) in raw MFM data.
 * Returns the byte offset after the sync pattern, or -1 if not found.
 *
 * The sync mark 0xA1 with missing clock encodes as:
 *   MFM bits: 0100 0100 1000 1001
 * In the byte stream, we look for the decoded sequence 0xA1, 0xA1, 0xA1
 * followed by the address mark byte (0xFE for IDAM, 0xFB for DAM).
 */
static int find_sync_mark(const uint8_t *data, uint16_t len, uint16_t start,
                           uint8_t mark) {
    /* Search for 3x sync byte 0xA1 followed by the address mark */
    for (uint16_t i = start; i < len - 3; i++) {
        if (data[i] == 0xA1 && data[i+1] == 0xA1 &&
            data[i+2] == 0xA1 && data[i+3] == mark) {
            return i + 4;  /* Return position after the mark */
        }
    }
    return -1;
}

int mfm_decode_sector(const uint8_t *raw_track, uint16_t raw_len,
                       uint8_t sector, uint8_t *data_out) {
    /*
     * Strategy:
     * 1. Find each IDAM (0xFE) in the track
     * 2. Read ID field: track, side, sector#, size
     * 3. Verify ID CRC
     * 4. If this is our sector, find the following DAM (0xFB)
     * 5. Read 512 bytes of data
     * 6. Verify data CRC
     *
     * Note: In the actual implementation, raw_track contains decoded bytes
     * after PIO MFM-to-data conversion. The PIO handles the actual MFM
     * cell timing → bit conversion.
     */

    uint16_t pos = 0;

    for (int attempt = 0; attempt < FLOPPY_SECTORS + 2; attempt++) {
        /* Find next IDAM */
        int idam_pos = find_sync_mark(raw_track, raw_len, pos, MFM_IDAM);
        if (idam_pos < 0) {
            return -1;  /* No more IDAMs found */
        }

        /* Read ID field */
        if (idam_pos + 6 > raw_len) return -1;

        uint8_t id_track  = raw_track[idam_pos];
        uint8_t id_side   = raw_track[idam_pos + 1];
        uint8_t id_sector = raw_track[idam_pos + 2];
        uint8_t id_size   = raw_track[idam_pos + 3];
        uint16_t id_crc   = (raw_track[idam_pos + 4] << 8) | raw_track[idam_pos + 5];

        /* Verify ID CRC (includes the 0xA1, 0xA1, 0xA1, 0xFE bytes) */
        uint16_t calc_crc = MFM_CRC_INIT;
        uint8_t sync_bytes[] = {0xA1, 0xA1, 0xA1, MFM_IDAM};
        calc_crc = mfm_crc16(sync_bytes, 4, calc_crc);
        calc_crc = mfm_crc16(&raw_track[idam_pos], 4, calc_crc);

        if (calc_crc != id_crc) {
            /* CRC error in ID field, skip to next */
            pos = idam_pos + 6;
            continue;
        }

        /* Is this the sector we want? */
        if (id_sector == sector) {
            /* Find the DAM after this ID field */
            int dam_pos = find_sync_mark(raw_track, raw_len,
                                          idam_pos + 6, MFM_DAM);
            if (dam_pos < 0) {
                return -1;  /* DAM not found */
            }

            /* Read 512 bytes of data */
            uint16_t data_size = 128 << id_size;  /* size_code 2 = 512 */
            if (dam_pos + data_size + 2 > raw_len) {
                return -1;
            }

            /* Copy sector data */
            for (uint16_t i = 0; i < data_size && i < 512; i++) {
                data_out[i] = raw_track[dam_pos + i];
            }

            /* Verify data CRC */
            uint16_t data_crc_calc = MFM_CRC_INIT;
            uint8_t dam_sync[] = {0xA1, 0xA1, 0xA1, MFM_DAM};
            data_crc_calc = mfm_crc16(dam_sync, 4, data_crc_calc);
            data_crc_calc = mfm_crc16(&raw_track[dam_pos], data_size, data_crc_calc);

            uint16_t data_crc_disk = (raw_track[dam_pos + data_size] << 8) |
                                      raw_track[dam_pos + data_size + 1];

            if (data_crc_calc != data_crc_disk) {
                return -2;  /* Data CRC error */
            }

            return 0;  /* Success */
        }

        pos = idam_pos + 6;
    }

    return -1;  /* Sector not found */
}

int mfm_encode_sector(const mfm_sector_id_t *id, const uint8_t *data,
                       uint8_t *mfm_out, uint16_t max_len) {
    uint16_t pos = 0;

    /* Gap 2 / pre-sync: 12 bytes of 0x00 */
    for (int i = 0; i < 12 && pos < max_len; i++) {
        mfm_out[pos++] = 0x00;
    }

    /* Sync: 3x 0xA1 with missing clock */
    for (int i = 0; i < 3 && pos < max_len; i++) {
        mfm_out[pos++] = 0xA1;
    }

    /* IDAM */
    if (pos < max_len) mfm_out[pos++] = MFM_IDAM;

    /* ID field */
    if (pos + 4 > max_len) return -1;
    mfm_out[pos++] = id->track;
    mfm_out[pos++] = id->side;
    mfm_out[pos++] = id->sector;
    mfm_out[pos++] = id->size_code;

    /* ID CRC */
    uint16_t crc = MFM_CRC_INIT;
    uint8_t sync_idam[] = {0xA1, 0xA1, 0xA1, MFM_IDAM,
                            id->track, id->side, id->sector, id->size_code};
    crc = mfm_crc16(sync_idam, 8, MFM_CRC_INIT);
    if (pos + 2 > max_len) return -1;
    mfm_out[pos++] = crc >> 8;
    mfm_out[pos++] = crc & 0xFF;

    /* Gap 2: 22 bytes of 0x4E */
    for (int i = 0; i < 22 && pos < max_len; i++) {
        mfm_out[pos++] = 0x4E;
    }

    /* Pre-data sync: 12 bytes of 0x00 */
    for (int i = 0; i < 12 && pos < max_len; i++) {
        mfm_out[pos++] = 0x00;
    }

    /* Sync: 3x 0xA1 */
    for (int i = 0; i < 3 && pos < max_len; i++) {
        mfm_out[pos++] = 0xA1;
    }

    /* DAM */
    if (pos < max_len) mfm_out[pos++] = MFM_DAM;

    /* Data: 512 bytes */
    for (int i = 0; i < 512 && pos < max_len; i++) {
        mfm_out[pos++] = data[i];
    }

    /* Data CRC */
    crc = MFM_CRC_INIT;
    uint8_t dam_sync[] = {0xA1, 0xA1, 0xA1, MFM_DAM};
    crc = mfm_crc16(dam_sync, 4, crc);
    crc = mfm_crc16(data, 512, crc);
    if (pos + 2 > max_len) return -1;
    mfm_out[pos++] = crc >> 8;
    mfm_out[pos++] = crc & 0xFF;

    /* Gap 3: 80 bytes of 0x4E */
    for (int i = 0; i < 80 && pos < max_len; i++) {
        mfm_out[pos++] = 0x4E;
    }

    return pos;
}

int mfm_encode_track(uint8_t track, uint8_t side,
                      uint8_t *mfm_out, uint16_t max_len) {
    uint16_t pos = 0;

    /* Gap 4a: 80 bytes of 0x4E */
    for (int i = 0; i < 80 && pos < max_len; i++) {
        mfm_out[pos++] = 0x4E;
    }

    /* Sync: 12 bytes of 0x00 */
    for (int i = 0; i < 12 && pos < max_len; i++) {
        mfm_out[pos++] = 0x00;
    }

    /* Index mark: 3x 0xC2 (with missing clock) + 0xFC */
    for (int i = 0; i < 3 && pos < max_len; i++) {
        mfm_out[pos++] = 0xC2;
    }
    if (pos < max_len) mfm_out[pos++] = 0xFC;

    /* Gap 1: 50 bytes of 0x4E */
    for (int i = 0; i < 50 && pos < max_len; i++) {
        mfm_out[pos++] = 0x4E;
    }

    /* 18 sectors */
    uint8_t empty_sector[512];
    memset(empty_sector, 0xE5, sizeof(empty_sector));  /* Fill with 0xE5 (format pattern) */

    for (uint8_t s = 1; s <= 18; s++) {
        mfm_sector_id_t id = {
            .track = track,
            .side = side,
            .sector = s,
            .size_code = 2,
        };

        int written = mfm_encode_sector(&id, empty_sector,
                                          &mfm_out[pos], max_len - pos);
        if (written < 0) return -1;
        pos += written;
    }

    /* Gap 4b: fill rest with 0x4E */
    while (pos < max_len) {
        mfm_out[pos++] = 0x4E;
    }

    return pos;
}

int mfm_find_sectors(const uint8_t *raw_track, uint16_t raw_len,
                      mfm_sector_id_t *ids_out, int max_ids) {
    int count = 0;
    uint16_t pos = 0;

    while (count < max_ids) {
        int idam_pos = find_sync_mark(raw_track, raw_len, pos, MFM_IDAM);
        if (idam_pos < 0 || idam_pos + 6 > raw_len) {
            break;
        }

        ids_out[count].track     = raw_track[idam_pos];
        ids_out[count].side      = raw_track[idam_pos + 1];
        ids_out[count].sector    = raw_track[idam_pos + 2];
        ids_out[count].size_code = raw_track[idam_pos + 3];
        ids_out[count].crc       = (raw_track[idam_pos + 4] << 8) |
                                    raw_track[idam_pos + 5];
        count++;
        pos = idam_pos + 6;
    }

    return count;
}
