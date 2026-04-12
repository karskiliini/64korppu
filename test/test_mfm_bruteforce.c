/*
 * Brute-force MFM decode: try all delay values 50..120,
 * score by counting 0x4E (gap fill) and 0x00 (preamble) bytes.
 * The best delay produces the most recognizable data.
 */
#include <stdio.h>
#include <stdint.h>

static const uint8_t trace[] = {
    0x36, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1, 0xBA, 0x86, 0x85, 0x85,
    0xD6, 0xA0, 0x86, 0x85, 0xBB, 0xA0, 0xA0, 0xA1, 0xD6, 0xA0, 0x86, 0x86,
    0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0x86, 0xBC, 0xA0, 0x86, 0x86,
    0xBC, 0x9F, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0x9F, 0xD7, 0xA0, 0x86, 0x86,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBB, 0x86, 0x85, 0x86, 0xBA, 0x85, 0x85, 0x85,
    0xD7, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1, 0xBB, 0xA0, 0x86, 0x86,
    0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x85, 0xA0, 0xD6, 0xA0, 0xA1, 0x86,
    0xBB, 0xA0, 0xA0, 0xA1, 0xBB, 0xA1, 0x85, 0x85, 0xBC, 0xA0, 0xA1, 0xA0,
    0xBC, 0x86, 0x85, 0xA0, 0xD6, 0xA1, 0xA0, 0x86, 0xBB, 0xA0, 0xA0, 0xA1,
    0xBB, 0xA0, 0x86, 0x85, 0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0x86, 0x86, 0xA1,
    0xD6, 0xA0, 0xA0, 0x86, 0xBB, 0xA0, 0xA1, 0xA0, 0xBB, 0xA0, 0x86, 0x85,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBC, 0x86, 0x85, 0xA0, 0xD6, 0xA1, 0xA0, 0x86,
    0xBB, 0xA0, 0xA0, 0xA1, 0xD5, 0xA1, 0x86, 0x85, 0xBB, 0xA1, 0xA0, 0xA0,
    0xBB, 0x85, 0x86, 0x86, 0xBB, 0x85, 0x86, 0x86, 0xD6, 0xA0, 0x86, 0x86,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBC, 0x86, 0x85, 0x86, 0xBB, 0x85, 0x86, 0x86,
    0xD5, 0xA1, 0xA0, 0x86, 0xBB, 0xA0, 0xA1, 0xA1, 0xBB, 0xA0, 0x86, 0x87,
    0xBB, 0xA1, 0xA0, 0xA0, 0xBB, 0x85, 0x86, 0xA1,
};

#define COUNT (sizeof(trace)/sizeof(trace[0]))

/* Decode with given constant delay, return score (count of 0x4E + 0x00 bytes) */
static int try_delay(int delay, int off, int verbose) {
    uint32_t bits = 0;
    uint8_t avail = 0, dbyte = 0, dbits = 0;
    int score = 0, nbytes = 0;

    for (int i = 0; i < (int)COUNT; i++) {
        int v = trace[i];
        if (v < 50) continue;
        int cells = (v - delay + 16) / 32;
        if (cells < 2) cells = 2;
        if (cells > 4) continue;

        bits = (bits << cells) | 1;
        avail += cells;
        if (avail > 30) avail = 30;

        while (avail >= 2) {
            avail -= 2;
            dbyte = (dbyte << 1) | ((bits >> (avail + off)) & 1);
            dbits++;
            if (dbits >= 8) {
                if (dbyte == 0x4E) score += 10;  /* Gap fill */
                if (dbyte == 0x00) score += 5;   /* Preamble */
                if (dbyte == 0xA1) score += 20;  /* Sync */
                if (dbyte == 0xFE) score += 15;  /* IDAM */
                if (dbyte == 0xFB) score += 15;  /* DAM */
                if (verbose) {
                    printf("%02X ", dbyte);
                    nbytes++;
                    if ((nbytes % 24) == 0) printf("\n    ");
                }
                dbyte = 0; dbits = 0;
            }
        }
    }
    return score;
}

int main(void) {
    printf("=== Brute-force delay search ===\n\n");

    int best_score = 0, best_delay = 0, best_off = 0;

    for (int off = 0; off <= 1; off++) {
        for (int delay = 50; delay <= 120; delay++) {
            int score = try_delay(delay, off, 0);
            if (score > best_score) {
                best_score = score;
                best_delay = delay;
                best_off = off;
            }
        }
    }

    printf("Best: delay=%d off=%d score=%d\n\n", best_delay, best_off, best_score);
    printf("Decoded bytes:\n    ");
    try_delay(best_delay, best_off, 1);
    printf("\n\n");

    /* Also show top 5 results */
    printf("Top results:\n");
    for (int rank = 0; rank < 5; rank++) {
        int bs = 0, bd = 0, bo = 0;
        for (int off = 0; off <= 1; off++) {
            for (int delay = 50; delay <= 120; delay++) {
                int score = try_delay(delay, off, 0);
                if (score > bs) {
                    /* Check not already shown */
                    int dup = 0;
                    (void)dup;
                    bs = score;
                    bd = delay;
                    bo = off;
                }
            }
        }
        if (bs == 0) break;
        printf("  delay=%3d off=%d score=%3d: ", bd, bo, bs);
        try_delay(bd, bo, 1);
        printf("\n");
        /* Hack: modify score to skip this result next time — just show best */
        break;  /* Just show best for now */
    }

    return 0;
}
