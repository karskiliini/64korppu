/*
 * 64korppu SimAVR simulation harness — end-to-end floppy read test.
 *
 * Runs the REAL compiled AVR firmware (floppy_reader.elf) cycle-
 * accurately with:
 *   - Virtual 23LC512/23LC256 SPI SRAM (capacity + address aliasing)
 *   - Virtual 74HC595 shift register (floppy control signals)
 *   - Virtual floppy drive: full IBM HD track (18 sectors, CRCs, gaps)
 *     generated with the independent encoder in test/mfm_track_gen.h,
 *     rotated continuously and fed as MFM flux pulses into ICP1 (PB0)
 *     with configurable motor speed error and per-edge jitter
 *   - /TRK00 stepping model, /WPT, /DSKCHG
 *   - UART capture with result parsing
 *
 * PASS criteria (exit code 0):
 *   - at least --reads N successful sector reads reported by firmware
 *   - after EVERY "OK", the 512-byte sector_buf in AVR RAM matches the
 *     generated sector payload byte-for-byte (verified via ELF symbol)
 *   - no "ERR" lines from the firmware
 *
 * Usage:
 *   sim_64korppu <floppy_reader.elf> [-t ms] [-reads N] [-v]
 *                [-speed-ppm P] [-jitter J] [-sram256]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_time.h>
#include <simavr/sim_cycle_timers.h>
#include <simavr/avr_spi.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_timer.h>

#include "mfm_track_gen.h"   /* shared with host tests */

#define PULSE_LOW_CYCLES 4   /* /RDATA low pulse ~250ns at 16MHz */

/* ── Virtual SPI SRAM (23LC512 default, 23LC256 with -sram256) ─────── */

#define SRAM_CMD_READ   0x03
#define SRAM_CMD_WRITE  0x02
#define SRAM_CMD_RDMR   0x05
#define SRAM_CMD_WRMR   0x01

typedef enum {
    SRAM_IDLE, SRAM_CMD_RECEIVED, SRAM_ADDR_HI, SRAM_ADDR_LO,
    SRAM_READING, SRAM_WRITING, SRAM_RDMR_RESP,
} sram_state_t;

static struct {
    uint8_t     memory[65536];
    uint32_t    capacity;       /* 65536 or 32768 (aliasing mask) */
    sram_state_t state;
    uint8_t     cmd;
    uint32_t    addr;
    uint8_t     mode;
    int         cs_active;
    uint32_t    total_reads;
    uint32_t    total_writes;
} sram;

/* ── Virtual 74HC595 ───────────────────────────────────────────────── */

static struct {
    uint8_t     shift_reg;
    uint8_t     output;
    int         rclk_prev;
    uint32_t    latch_count;
} sr595;

/* ── Virtual floppy drive ──────────────────────────────────────────── */

static struct {
    int         track;
    int         motor_on;
    int         selected;
    int         disk_inserted;
    uint32_t    step_count;

    /* MFM rotation model */
    tg_track_t  image;                       /* interval stream, T0/S0 */
    uint8_t     data[TG_SECTORS][TG_SECTOR_SIZE];
    uint64_t    *edge_cycle;                 /* cumulative edge offsets */
    uint64_t    rev_cycles;                  /* cycles per revolution */
    uint64_t    epoch;                       /* cycle when rotation began */
    int         edge_idx;                    /* next edge to emit */
    int         emitting;                    /* timer active */
    int         pin_low;                     /* current PB0 drive state */
} floppy;

static int speed_ppm = 0;    /* motor speed error, e.g. +15000 = +1.5% */
static int jitter_ticks = 0; /* ± per-edge jitter in 62.5ns ticks */
static int wobble_ppm = 0;   /* sinusoidal once-per-rev speed wobble
                                (eccentric disk clamping), amplitude */

/* ── Simulation state ──────────────────────────────────────────────── */

static avr_t *avr = NULL;
static volatile int running = 1;
static int verbose = 0;

static uint32_t sector_buf_addr = 0;  /* AVR RAM address from ELF symbol */

/* Result tracking (parsed from firmware UART output) */
static int ok_reads = 0;
static int err_reads = 0;
static int ram_mismatch = 0;
static int reads_target = 3;
static int allow_err = 0;   /* tolerated failed reads (32KB coverage) */

static const char *sr_bit_names[] = {
    "SIDE1", "DENSITY", "MOTEA", "DRVSEL", "MOTOR", "DIR", "STEP", "WGATE",
};

/* ── RDATA edge scheduling ─────────────────────────────────────────── */

static int rdata_active(void) {
    return floppy.motor_on && floppy.selected && floppy.disk_inserted &&
           floppy.track == 0;
}

static void rdata_set_pin(int low) {
    floppy.pin_low = low;
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0),
                  low ? 0 : 1);
}

static avr_cycle_count_t rdata_timer_cb(avr_t *a, avr_cycle_count_t when,
                                        void *param);

/* Find the first edge at or after the current rotation angle and
 * schedule it. */
static void rdata_start(void) {
    if (floppy.emitting || !rdata_active()) return;

    uint64_t angle = (avr->cycle - floppy.epoch) % floppy.rev_cycles;
    /* binary search first edge_cycle > angle */
    int lo = 0, hi = floppy.image.count - 1, idx = 0;
    if (floppy.edge_cycle[hi] <= angle) {
        idx = 0;  /* wrap to next revolution */
    } else {
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (floppy.edge_cycle[mid] > angle) hi = mid; else lo = mid + 1;
        }
        idx = lo;
    }
    floppy.edge_idx = idx;

    uint64_t edge_abs = floppy.epoch +
        ((avr->cycle - floppy.epoch) / floppy.rev_cycles) * floppy.rev_cycles +
        floppy.edge_cycle[idx];
    while (edge_abs <= avr->cycle) edge_abs += floppy.rev_cycles;

    floppy.emitting = 1;
    avr_cycle_timer_register(avr, edge_abs - avr->cycle, rdata_timer_cb, NULL);
    if (verbose)
        printf("[FLOPPY] RDATA start: edge %d in %llu cycles\n",
               idx, (unsigned long long)(edge_abs - avr->cycle));
}

static void rdata_stop(void) {
    if (!floppy.emitting) return;
    floppy.emitting = 0;
    avr_cycle_timer_cancel(avr, rdata_timer_cb, NULL);
    if (floppy.pin_low) rdata_set_pin(0);
}

/* One timer, two phases: falling edge → +PULSE_LOW_CYCLES rising edge
 * → next falling edge from the cumulative table. */
static avr_cycle_count_t rdata_timer_cb(avr_t *a, avr_cycle_count_t when,
                                        void *param) {
    (void)a; (void)param;
    if (!floppy.emitting) return 0;

    if (!floppy.pin_low) {
        /* Emit falling edge (flux transition) */
        rdata_set_pin(1);
        return when + PULSE_LOW_CYCLES;
    }

    /* Rising edge; schedule next transition */
    rdata_set_pin(0);
    int i = floppy.edge_idx;
    uint64_t cur = floppy.edge_cycle[i];
    int next = (i + 1) % floppy.image.count;
    uint64_t gap = (next == 0)
        ? floppy.rev_cycles - cur + floppy.edge_cycle[0]
        : floppy.edge_cycle[next] - cur;
    floppy.edge_idx = next;
    if (gap <= PULSE_LOW_CYCLES) gap = PULSE_LOW_CYCLES + 1;
    return when - PULSE_LOW_CYCLES + gap;
}

/* Build the rotation tables from the generated interval stream */
static void floppy_build_rotation(void) {
    tg_fill_pattern(floppy.data);
    tg_build_track(&floppy.image, 0, 0,
                   (const uint8_t (*)[TG_SECTOR_SIZE])floppy.data, 0, 0);

    floppy.edge_cycle = calloc(floppy.image.count, sizeof(uint64_t));
    double speed = 1.0 + speed_ppm / 1e6;
    double wobble = wobble_ppm / 1e6;
    uint64_t acc = 0;
    tg_srand(0xF10BB7);
    for (int i = 0; i < floppy.image.count; i++) {
        double s = speed *
            (1.0 + wobble * sin(2.0 * M_PI * i / floppy.image.count));
        double ticks = floppy.image.cells[i] * 16.0 * s;
        long t = lrint(ticks);
        if (jitter_ticks)
            t += (long)(tg_rand() % (2 * jitter_ticks + 1)) - jitter_ticks;
        if (t < PULSE_LOW_CYCLES + 2) t = PULSE_LOW_CYCLES + 2;
        acc += (uint64_t)t;
        floppy.edge_cycle[i] = acc;
    }
    floppy.rev_cycles = acc;
    printf("[SIM] track: %d flux transitions, %.2f ms/rev "
           "(speed %+d ppm, wobble ±%d ppm, jitter ±%d ticks)\n",
           floppy.image.count, floppy.rev_cycles / 16000.0,
           speed_ppm, wobble_ppm, jitter_ticks);
}

/* ── UART capture + result parsing ─────────────────────────────────── */

static void check_sector_ram(void) {
    if (!sector_buf_addr) return;
    const uint8_t *ram = avr->data + sector_buf_addr;
    if (memcmp(ram, floppy.data[0], TG_SECTOR_SIZE) != 0) {
        ram_mismatch++;
        printf("[SIM] *** sector_buf MISMATCH vs generated data! ***\n");
        for (int i = 0; i < TG_SECTOR_SIZE; i++) {
            if (ram[i] != floppy.data[0][i]) {
                printf("[SIM]   first diff at %d: got %02X want %02X\n",
                       i, ram[i], floppy.data[0][i]);
                break;
            }
        }
    } else {
        printf("[SIM] sector_buf verified: 512/512 bytes correct\n");
    }
}

static char uart_line[256];
static int uart_line_len = 0;

static void uart_output_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    char c = (char)value;
    putchar(c);
    fflush(stdout);

    if (c == '\n' || uart_line_len >= (int)sizeof(uart_line) - 1) {
        uart_line[uart_line_len] = 0;
        /* floppy_reader prints "<count> OK" or "<count> ERR <code>" */
        int n;
        char word[8] = {0};
        if (sscanf(uart_line, "%d %7s", &n, word) == 2) {
            if (strcmp(word, "OK") == 0) {
                ok_reads++;
                check_sector_ram();
            } else if (strcmp(word, "ERR") == 0) {
                err_reads++;
            }
        }
        uart_line_len = 0;
    } else if (c != '\r') {
        uart_line[uart_line_len++] = c;
    }
}

/* ── SPI hook (SRAM emulation) ─────────────────────────────────────── */

static void spi_output_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    uint8_t byte_out = value & 0xFF;
    uint8_t byte_in = 0xFF;

    if (!sram.cs_active) {
        /* /CS high — byte goes to the 74HC595 */
        sr595.shift_reg = byte_out;
        avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), 0), byte_in);
        return;
    }

    switch (sram.state) {
    case SRAM_IDLE:
        sram.cmd = byte_out;
        sram.addr = 0;
        if (byte_out == SRAM_CMD_WRMR) sram.state = SRAM_CMD_RECEIVED;
        else if (byte_out == SRAM_CMD_RDMR) sram.state = SRAM_RDMR_RESP;
        else if (byte_out == SRAM_CMD_READ || byte_out == SRAM_CMD_WRITE)
            sram.state = SRAM_ADDR_HI;
        break;
    case SRAM_CMD_RECEIVED:
        sram.mode = byte_out;
        sram.state = SRAM_IDLE;
        break;
    case SRAM_RDMR_RESP:
        byte_in = sram.mode;
        sram.state = SRAM_IDLE;
        break;
    case SRAM_ADDR_HI:
        sram.addr = (uint32_t)byte_out << 8;
        sram.state = SRAM_ADDR_LO;
        break;
    case SRAM_ADDR_LO:
        sram.addr |= byte_out;
        sram.state = (sram.cmd == SRAM_CMD_READ) ? SRAM_READING : SRAM_WRITING;
        break;
    case SRAM_READING:
        /* Real chips ignore high address bits → alias, don't clip */
        byte_in = sram.memory[sram.addr & (sram.capacity - 1)];
        sram.addr++;
        sram.total_reads++;
        break;
    case SRAM_WRITING:
        sram.memory[sram.addr & (sram.capacity - 1)] = byte_out;
        sram.addr++;
        sram.total_writes++;
        break;
    }

    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), 0), byte_in);
}

/* ── /CS_SRAM on PB2 ───────────────────────────────────────────────── */

static void cs_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    int prev = sram.cs_active;
    sram.cs_active = !(value & 1);
    if (prev != sram.cs_active)
        sram.state = SRAM_IDLE;
}

/* ── RCLK on PD6: latch 74HC595, drive floppy control model ───────── */

static void rclk_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    int rclk = value & 1;

    if (rclk && !sr595.rclk_prev) {
        uint8_t prev = sr595.output;
        sr595.output = sr595.shift_reg;
        sr595.latch_count++;

        uint8_t changed = prev ^ sr595.output;
        if (changed && verbose) {
            printf("[595] latch: 0x%02X -> 0x%02X ", prev, sr595.output);
            for (int i = 0; i < 8; i++)
                if (changed & (1 << i))
                    printf(" %s=%s", sr_bit_names[i],
                           !(sr595.output & (1 << i)) ? "ON" : "off");
            printf("\n");
        }

        /* MOTOR (bit 4) + DRVSEL (bit 3), active low */
        int motor = !(sr595.output & (1 << 4));
        int drvsel = !(sr595.output & (1 << 3));
        if (motor && !floppy.motor_on) {
            floppy.motor_on = 1;
            floppy.epoch = avr->cycle;   /* rotation starts */
            printf("[FLOPPY] Motor ON\n");
        } else if (!motor && floppy.motor_on) {
            floppy.motor_on = 0;
            printf("[FLOPPY] Motor OFF\n");
        }
        floppy.selected = drvsel;

        if (rdata_active()) rdata_start(); else rdata_stop();

        /* STEP pulse (bit 6, falling = assert) with DIR (bit 5) */
        if ((changed & (1 << 6)) && !(sr595.output & (1 << 6))) {
            int dir_in = !(sr595.output & (1 << 5));
            floppy.track += dir_in ? 1 : -1;
            if (floppy.track < 0) floppy.track = 0;
            if (floppy.track > 79) floppy.track = 79;
            floppy.step_count++;
            if (verbose)
                printf("[FLOPPY] Step %s -> track %d\n",
                       dir_in ? "IN" : "OUT", floppy.track);

            /* /TRK00 on PC0 (active low) */
            avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0),
                          floppy.track != 0);
            if (rdata_active()) rdata_start(); else rdata_stop();
        }
    }
    sr595.rclk_prev = rclk;
}

/* ── Main ──────────────────────────────────────────────────────────── */

static void sigint_handler(int sig) { (void)sig; running = 0; }

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <floppy_reader.elf> [options]\n"
            "  -t <ms>        max simulated time (default 4000)\n"
            "  -reads <n>     required successful reads (default 3)\n"
            "  -speed-ppm <p> motor speed error, e.g. 15000 = +1.5%%\n"
            "  -jitter <t>    per-edge jitter in 62.5ns ticks\n"
            "  -sram256       simulate 23LC256 (32KB, aliasing)\n"
            "  -v             verbose\n", argv[0]);
        return 2;
    }

    const char *elf_path = argv[1];
    int sim_ms = 4000;

    memset(&sram, 0, sizeof(sram));
    sram.capacity = 65536;

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) verbose = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            sim_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-reads") && i + 1 < argc)
            reads_target = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-speed-ppm") && i + 1 < argc)
            speed_ppm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-jitter") && i + 1 < argc)
            jitter_ticks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-wobble-ppm") && i + 1 < argc)
            wobble_ppm = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-allow-err") && i + 1 < argc)
            allow_err = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-sram256"))
            sram.capacity = 32768;
    }

    elf_firmware_t fw = {};
    if (elf_read_firmware(elf_path, &fw) != 0) {
        fprintf(stderr, "Error: cannot read ELF: %s\n", elf_path);
        return 2;
    }
    if (fw.mmcu[0] == 0) strcpy(fw.mmcu, "atmega328p");
    if (fw.frequency == 0) fw.frequency = 16000000;

    /* Locate sector_buf in AVR RAM for byte-exact verification */
    for (uint32_t s = 0; s < fw.symbolcount; s++) {
        if (!strcmp(fw.symbol[s]->symbol, "sector_buf")) {
            sector_buf_addr = fw.symbol[s]->addr & 0xFFFFFF;
            if (sector_buf_addr >= 0x800000) sector_buf_addr -= 0x800000;
            break;
        }
    }
    printf("[SIM] firmware: %s (%u bytes flash), sector_buf @ 0x%04X\n",
           elf_path, fw.flashsize, sector_buf_addr);
    printf("[SIM] SRAM model: %u KB%s\n", sram.capacity / 1024,
           sram.capacity == 32768 ? " (23LC256, aliasing)" : " (23LC512)");
    if (!sector_buf_addr) {
        fprintf(stderr, "Error: sector_buf symbol not found in ELF\n");
        return 2;
    }

    avr = avr_make_mcu_by_name(fw.mmcu);
    if (!avr) { fprintf(stderr, "Unknown MCU\n"); return 2; }
    avr_init(avr);
    avr_load_firmware(avr, &fw);

    memset(&sr595, 0, sizeof(sr595));
    sr595.output = 0xFF;
    memset(&floppy, 0, sizeof(floppy));
    floppy.disk_inserted = 1;
    floppy.track = 40;          /* head starts somewhere on the disk */
    floppy_build_rotation();

    /* Idle input pin states: /TRK00 hi (not track0), /WPT hi, /DSKCHG hi,
     * /RDATA hi */
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 1), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 2), 1);
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0), 1);

    /* Silence simavr's built-in UART stdout echo (we print ourselves) */
    {
        uint32_t flags = 0;
        avr_ioctl(avr, AVR_IOCTL_UART_GET_FLAGS('0'), &flags);
        flags &= ~AVR_UART_FLAG_STDIO;
        avr_ioctl(avr, AVR_IOCTL_UART_SET_FLAGS('0'), &flags);
    }
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        uart_output_hook, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT),
        spi_output_hook, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 2),
        cs_hook, NULL);
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), 6),
        rclk_hook, NULL);

    signal(SIGINT, sigint_handler);

    printf("--- Firmware UART output ---\n");
    uint64_t end_cycle = (uint64_t)sim_ms * (fw.frequency / 1000);
    while (running && avr->cycle < end_cycle) {
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed) {
            printf("\n[SIM] AVR %s\n",
                   state == cpu_Done ? "halted" : "CRASHED");
            break;
        }
        if (ok_reads >= reads_target && !ram_mismatch)
            break;   /* early success */
        if (ram_mismatch || err_reads > allow_err)
            break;   /* early failure */
    }

    printf("\n--- Result ---\n");
    printf("[SIM] simulated %.1f ms, OK reads: %d (target %d), "
           "ERR reads: %d, RAM mismatches: %d\n",
           avr->cycle / 16000.0, ok_reads, reads_target,
           err_reads, ram_mismatch);
    printf("[SIM] SRAM traffic: %u reads, %u writes; 595 latches: %u; "
           "steps: %u\n",
           sram.total_reads, sram.total_writes, sr595.latch_count,
           floppy.step_count);

    int pass = ok_reads >= reads_target && !ram_mismatch &&
               err_reads <= allow_err;
    printf("[SIM] %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
