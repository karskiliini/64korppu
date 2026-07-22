/*
 * IBM/PC 3.5" HD (1.44MB) MFM track generator for tests.
 *
 * Builds a full 18-sector track (gap4a + per-sector ID/data fields
 * with CRCs + gaps) and MFM-encodes it into a circular stream of
 * flux-transition intervals expressed in half-cells (2/3/4).
 *
 * DELIBERATELY independent of the firmware: the CRC here is a
 * separate table-driven CRC-CCITT implementation, and the MFM encoder
 * is written from the format spec, not copied from mfm_codec.c. The
 * firmware decoding these streams byte-exactly therefore cross-checks
 * both implementations against each other.
 *
 * Header-only so the host tests and the simavr harness share it.
 */
#ifndef MFM_TRACK_GEN_H
#define MFM_TRACK_GEN_H

#include <stdint.h>
#include <string.h>

#define TG_SECTORS        18
#define TG_SECTOR_SIZE    512
#define TG_MAX_INTERVALS  90000
#define TG_TRACK_BYTES    12500   /* raw bytes per revolution at HD */

typedef struct {
    uint8_t cells[TG_MAX_INTERVALS];  /* interval lengths in half-cells */
    int     count;                    /* number of intervals (circular) */
} tg_track_t;

/* ---- Independent CRC-CCITT (table-driven, init 0xFFFF) ---- */

static uint16_t tg_crc_table[256];
static int tg_crc_table_ready = 0;

static void tg_crc_init_table(void) {
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; b++)
            c = (uint16_t)((c & 0x8000) ? (c << 1) ^ 0x1021 : (c << 1));
        tg_crc_table[i] = c;
    }
    tg_crc_table_ready = 1;
}

static uint16_t tg_crc(const uint8_t *data, int len, uint16_t crc) {
    if (!tg_crc_table_ready) tg_crc_init_table();
    for (int i = 0; i < len; i++)
        crc = (uint16_t)((crc << 8) ^ tg_crc_table[((crc >> 8) ^ data[i]) & 0xFF]);
    return crc;
}

/* ---- MFM bit-level encoder ---- */

typedef struct {
    tg_track_t *out;
    int gap_since_last_pulse;  /* half-cells since previous flux pulse */
    int prev_data_bit;
} tg_enc_t;

static void tg_emit_halfcell(tg_enc_t *e, int pulse) {
    e->gap_since_last_pulse++;
    if (pulse) {
        if (e->out->count < TG_MAX_INTERVALS)
            e->out->cells[e->out->count++] = (uint8_t)e->gap_since_last_pulse;
        e->gap_since_last_pulse = 0;
    }
}

/* Encode one data byte as MFM: for each bit, clock half-cell then
 * data half-cell. Clock pulse when prev_data==0 && bit==0. */
static void tg_enc_byte(tg_enc_t *e, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        int bit = (byte >> i) & 1;
        tg_emit_halfcell(e, !e->prev_data_bit && !bit);  /* clock */
        tg_emit_halfcell(e, bit);                        /* data */
        e->prev_data_bit = bit;
    }
}

/* Encode the 0xA1 sync byte with missing clock: raw pattern 0x4489 */
static void tg_enc_sync_a1(tg_enc_t *e) {
    uint16_t pat = 0x4489;
    for (int i = 15; i >= 0; i--)
        tg_emit_halfcell(e, (pat >> i) & 1);
    e->prev_data_bit = 1;  /* 0xA1 ends in data bit 1 */
}

/*
 * Build a full track.
 *  - data[r-1][0..511] is the payload of sector r (1..18)
 *  - corrupt_data_crc: bitmask; bit (r-1) set → sector r's data-field
 *    CRC is written wrong (decoder must reject it)
 *  - corrupt_id_crc: same for the ID field CRC
 */
static void tg_build_track(tg_track_t *t, uint8_t track_no, uint8_t side,
                           const uint8_t data[TG_SECTORS][TG_SECTOR_SIZE],
                           uint32_t corrupt_data_crc,
                           uint32_t corrupt_id_crc) {
    t->count = 0;
    tg_enc_t e = { .out = t, .gap_since_last_pulse = 0, .prev_data_bit = 0 };

    int bytes = 0;
    /* Gap 4a */
    for (int i = 0; i < 80; i++) { tg_enc_byte(&e, 0x4E); bytes++; }

    for (int r = 1; r <= TG_SECTORS; r++) {
        /* ID field */
        for (int i = 0; i < 12; i++) { tg_enc_byte(&e, 0x00); bytes++; }
        tg_enc_sync_a1(&e); tg_enc_sync_a1(&e); tg_enc_sync_a1(&e);
        bytes += 3;
        uint8_t idf[8] = { 0xA1, 0xA1, 0xA1, 0xFE,
                           track_no, side, (uint8_t)r, 2 };
        tg_enc_byte(&e, 0xFE);
        tg_enc_byte(&e, track_no);
        tg_enc_byte(&e, side);
        tg_enc_byte(&e, (uint8_t)r);
        tg_enc_byte(&e, 2);
        bytes += 5;
        uint16_t icrc = tg_crc(idf, 8, 0xFFFF);
        if (corrupt_id_crc & (1u << (r - 1))) icrc ^= 0x5555;
        tg_enc_byte(&e, (uint8_t)(icrc >> 8));
        tg_enc_byte(&e, (uint8_t)(icrc & 0xFF));
        bytes += 2;

        /* Gap 2 */
        for (int i = 0; i < 22; i++) { tg_enc_byte(&e, 0x4E); bytes++; }

        /* Data field */
        for (int i = 0; i < 12; i++) { tg_enc_byte(&e, 0x00); bytes++; }
        tg_enc_sync_a1(&e); tg_enc_sync_a1(&e); tg_enc_sync_a1(&e);
        bytes += 3;
        tg_enc_byte(&e, 0xFB);
        bytes++;
        uint8_t hdr[4] = { 0xA1, 0xA1, 0xA1, 0xFB };
        uint16_t dcrc = tg_crc(hdr, 4, 0xFFFF);
        for (int i = 0; i < TG_SECTOR_SIZE; i++) {
            tg_enc_byte(&e, data[r - 1][i]);
            bytes++;
        }
        dcrc = tg_crc(data[r - 1], TG_SECTOR_SIZE, dcrc);
        if (corrupt_data_crc & (1u << (r - 1))) dcrc ^= 0x5555;
        tg_enc_byte(&e, (uint8_t)(dcrc >> 8));
        tg_enc_byte(&e, (uint8_t)(dcrc & 0xFF));
        bytes += 2;

        /* Gap 3 */
        for (int i = 0; i < 54; i++) { tg_enc_byte(&e, 0x4E); bytes++; }
    }

    /* Gap 4b: pad the revolution */
    while (bytes < TG_TRACK_BYTES) { tg_enc_byte(&e, 0x4E); bytes++; }
}

/* Deterministic per-sector test payload */
static void tg_fill_pattern(uint8_t data[TG_SECTORS][TG_SECTOR_SIZE]) {
    for (int r = 0; r < TG_SECTORS; r++)
        for (int i = 0; i < TG_SECTOR_SIZE; i++)
            data[r][i] = (uint8_t)((r + 1) * 31 + i * 7 + (i >> 6));
}

/* ---- Small deterministic PRNG (xorshift) for jitter/noise ---- */

static uint32_t tg_rng_state = 1;
static void tg_srand(uint32_t seed) { tg_rng_state = seed ? seed : 1; }
static uint32_t tg_rand(void) {
    uint32_t x = tg_rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return tg_rng_state = x;
}

#endif /* MFM_TRACK_GEN_H */
