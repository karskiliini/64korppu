#ifndef MFM_CODEC_H
#define MFM_CODEC_H

#include <stdint.h>
#include <stdbool.h>

/*
 * MFM (Modified Frequency Modulation) encoding/decoding for PC floppy disks.
 *
 * MFM encoding rules:
 * - A '1' data bit is always encoded as a flux transition: 01
 * - A '0' data bit is encoded as 10 if previous data bit was 0 (clock bit),
 *   or 00 if previous data bit was 1 (no clock bit)
 *
 * PC HD floppy: 500 kbit/s data rate, 2us cell time
 * One sector on disk: ID field + gap + data field
 *
 * Sector format (IBM MFM):
 *   Gap 3 (variable)
 *   Sync: 12 bytes of 0x00
 *   IDAM: 3x 0xA1 (with missing clock) + 0xFE
 *   ID field: track, side, sector, size code (2=512)
 *   CRC: 2 bytes (CRC-CCITT)
 *   Gap 2: 22 bytes of 0x4E
 *   Sync: 12 bytes of 0x00
 *   DAM: 3x 0xA1 (with missing clock) + 0xFB
 *   Data: 512 bytes
 *   CRC: 2 bytes (CRC-CCITT)
 *   Gap 3: ~80 bytes of 0x4E
 */

/* Address mark patterns (with missing clock bits) */
#define MFM_SYNC_BYTE       0xA1    /* Sync byte with missing clock */
#define MFM_IDAM            0xFE    /* ID Address Mark */
#define MFM_DAM             0xFB    /* Data Address Mark */
#define MFM_DDAM            0xF8    /* Deleted Data Address Mark */

/* Sector ID field */
typedef struct {
    uint8_t track;
    uint8_t side;
    uint8_t sector;      /* 1-based */
    uint8_t size_code;   /* 2 = 512 bytes */
    uint16_t crc;
} mfm_sector_id_t;

/* CRC-CCITT (polynomial 0x1021, initial value 0xFFFF) */
#define MFM_CRC_INIT   0xFFFF
#define MFM_CRC_POLY   0x1021

/**
 * Initialize MFM PIO state machines for reading and writing.
 */
void mfm_init(void);

/**
 * Calculate CRC-CCITT over a buffer.
 * @param data    Data buffer.
 * @param length  Number of bytes.
 * @param crc     Initial CRC value (use MFM_CRC_INIT for start).
 * @return Updated CRC value.
 */
uint16_t mfm_crc16(const uint8_t *data, uint16_t length, uint16_t crc);

/**
 * Decode MFM-encoded raw track data and extract a specific sector.
 * Searches for the sector's IDAM, verifies ID CRC, then reads data field.
 *
 * @param raw_track  Raw MFM track data.
 * @param raw_len    Length of raw track data in bytes.
 * @param sector     Sector number to find (1-18).
 * @param data_out   Buffer for decoded 512-byte sector data.
 * @return 0 on success, negative on error.
 */
int mfm_decode_sector(const uint8_t *raw_track, uint16_t raw_len,
                       uint8_t sector, uint8_t *data_out);

/**
 * Encode a sector into MFM format for writing.
 * Generates: sync + IDAM + ID + CRC + gap2 + sync + DAM + data + CRC
 *
 * @param id         Sector ID fields.
 * @param data       512 bytes of sector data.
 * @param mfm_out    Output buffer for MFM-encoded data.
 * @param max_len    Maximum output buffer size.
 * @return Number of MFM bytes generated, or negative on error.
 */
int mfm_encode_sector(const mfm_sector_id_t *id, const uint8_t *data,
                       uint8_t *mfm_out, uint16_t max_len);

/**
 * Encode a full track (18 sectors) for formatting.
 * Generates: gap4a + sync + index + gap1 + [18 sectors with gaps] + gap4b
 *
 * @param track      Track number.
 * @param side       Side number.
 * @param mfm_out    Output buffer for full MFM track.
 * @param max_len    Maximum output buffer size.
 * @return Number of MFM bytes generated, or negative on error.
 */
int mfm_encode_track(uint8_t track, uint8_t side,
                      uint8_t *mfm_out, uint16_t max_len);

/**
 * Find all sector IDs in a raw track.
 * @param raw_track  Raw MFM track data.
 * @param raw_len    Length of raw data.
 * @param ids_out    Array of sector IDs found.
 * @param max_ids    Maximum number of IDs to return.
 * @return Number of sectors found.
 */
int mfm_find_sectors(const uint8_t *raw_track, uint16_t raw_len,
                      mfm_sector_id_t *ids_out, int max_ids);

#endif /* MFM_CODEC_H */
