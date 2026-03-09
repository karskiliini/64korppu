/*
 * 64korppu SimAVR simulation harness.
 *
 * Simulates ATmega328P running the 64korppu firmware with:
 *   - Virtual 23LC256 32KB SPI SRAM
 *   - Virtual 74HC595 shift register (floppy control signals)
 *   - Virtual floppy drive (TRK00, WPT, DSKCHG, RDATA)
 *   - Virtual IEC bus (ATN, CLK, DATA stimulus)
 *   - UART capture (serial debug output)
 *
 * Build: make -C tools/sim
 * Run:   tools/sim/sim_64korppu firmware/E-IEC-Nano-SRAM/64korppu_nano_debug.elf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <simavr/sim_avr.h>
#include <simavr/sim_elf.h>
#include <simavr/sim_time.h>
#include <simavr/avr_spi.h>
#include <simavr/avr_ioport.h>
#include <simavr/avr_uart.h>
#include <simavr/avr_timer.h>

/* ── Virtual 23LC256 SPI SRAM ──────────────────────────────────────── */

#define SRAM_SIZE       (32 * 1024)
#define SRAM_CMD_READ   0x03
#define SRAM_CMD_WRITE  0x02
#define SRAM_CMD_RDMR   0x05
#define SRAM_CMD_WRMR   0x01

typedef enum {
    SRAM_IDLE,
    SRAM_CMD_RECEIVED,
    SRAM_ADDR_HI,
    SRAM_ADDR_LO,
    SRAM_READING,
    SRAM_WRITING,
    SRAM_RDMR_RESP,
} sram_state_t;

static struct {
    uint8_t     memory[SRAM_SIZE];
    sram_state_t state;
    uint8_t     cmd;
    uint32_t    addr;
    uint8_t     mode;           /* 0x00=byte, 0x40=sequential, 0x80=page */
    int         cs_active;      /* /CS low = active */
    int         addr_byte;
    uint32_t    total_reads;
    uint32_t    total_writes;
} sram;

/* ── Virtual 74HC595 Shift Register ────────────────────────────────── */

static struct {
    uint8_t     shift_reg;      /* Current shift register contents */
    uint8_t     output;         /* Latched output (after RCLK pulse) */
    int         rclk_prev;      /* Previous RCLK state for edge detect */
    uint32_t    latch_count;
} sr595;

/* ── Virtual Floppy Drive ──────────────────────────────────────────── */

static struct {
    int         track;
    int         side;
    int         motor_on;
    int         disk_inserted;
    int         write_protected;
    uint32_t    step_count;
    uint32_t    motor_cycles;
} floppy;

/* ── Simulation state ──────────────────────────────────────────────── */

static avr_t *avr = NULL;
static volatile int running = 1;
static uint64_t boot_cycle = 0;
static int verbose = 0;

/* ── Signal names for 74HC595 ──────────────────────────────────────── */

static const char *sr_bit_names[] = {
    "SIDE1",   "DENSITY", "MOTEA",  "DRVSEL",
    "MOTOR",   "DIR",     "STEP",   "WGATE",
};

/* ── UART capture ──────────────────────────────────────────────────── */

static void uart_output_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    char c = (char)value;
    putchar(c);
    fflush(stdout);
}

/* ── SPI hook (23LC256 SRAM emulation) ─────────────────────────────── */

static void spi_output_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    uint8_t byte_out = value & 0xFF;
    uint8_t byte_in = 0xFF;  /* Default MISO response */

    if (!sram.cs_active) {
        /* /CS is high — this SPI byte goes to 74HC595, not SRAM */
        sr595.shift_reg = byte_out;
        if (verbose)
            printf("[595] SPI shift: 0x%02X\n", byte_out);
        /* MISO from SRAM is hi-Z when /CS high */
        avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), 0),
                      byte_in);
        return;
    }

    /* SRAM /CS is low — handle SRAM protocol */
    switch (sram.state) {
    case SRAM_IDLE:
        sram.cmd = byte_out;
        sram.addr = 0;
        sram.addr_byte = 0;
        if (byte_out == SRAM_CMD_WRMR) {
            sram.state = SRAM_CMD_RECEIVED;
        } else if (byte_out == SRAM_CMD_RDMR) {
            sram.state = SRAM_RDMR_RESP;
        } else if (byte_out == SRAM_CMD_READ || byte_out == SRAM_CMD_WRITE) {
            sram.state = SRAM_ADDR_HI;
        } else {
            if (verbose)
                printf("[SRAM] Unknown cmd: 0x%02X\n", byte_out);
        }
        break;

    case SRAM_CMD_RECEIVED:
        /* WRMR: next byte is mode register */
        sram.mode = byte_out;
        if (verbose)
            printf("[SRAM] Mode set: 0x%02X (%s)\n", byte_out,
                   byte_out == 0x40 ? "sequential" :
                   byte_out == 0x80 ? "page" : "byte");
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
        if (verbose)
            printf("[SRAM] %s addr=0x%04X\n",
                   sram.cmd == SRAM_CMD_READ ? "READ" : "WRITE",
                   sram.addr);
        sram.state = (sram.cmd == SRAM_CMD_READ) ?
                     SRAM_READING : SRAM_WRITING;
        break;

    case SRAM_READING:
        if (sram.addr < SRAM_SIZE) {
            byte_in = sram.memory[sram.addr];
        }
        sram.addr = (sram.addr + 1) & (SRAM_SIZE - 1);
        sram.total_reads++;
        break;

    case SRAM_WRITING:
        if (sram.addr < SRAM_SIZE) {
            sram.memory[sram.addr] = byte_out;
        }
        sram.addr = (sram.addr + 1) & (SRAM_SIZE - 1);
        sram.total_writes++;
        break;
    }

    /* Send MISO byte back via SPI input IRQ */
    avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), 0),
                  byte_in);
}

/* ── Port B hook (/CS_SRAM on PB2) ────────────────────────────────── */

static void portb_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    int cs = (value >> 2) & 1;  /* PB2 = /CS_SRAM */
    int prev_cs = sram.cs_active;

    sram.cs_active = !cs;  /* Active low */

    if (prev_cs && !sram.cs_active) {
        /* /CS went high — deselect SRAM, reset state */
        sram.state = SRAM_IDLE;
    }
    if (!prev_cs && sram.cs_active) {
        /* /CS went low — SRAM selected */
        sram.state = SRAM_IDLE;
    }
}

/* ── Port D hook (RCLK on PD6, IEC on PD2-5) ─────────────────────── */

static void portd_hook(struct avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq; (void)param;
    int rclk = (value >> 6) & 1;  /* PD6 = RCLK */

    /* Detect rising edge of RCLK → latch shift register */
    if (rclk && !sr595.rclk_prev) {
        uint8_t prev = sr595.output;
        sr595.output = sr595.shift_reg;
        sr595.latch_count++;

        /* Report changes */
        uint8_t changed = prev ^ sr595.output;
        if (changed) {
            printf("[595] Latch #%u: 0x%02X → 0x%02X  ",
                   sr595.latch_count, prev, sr595.output);
            for (int i = 0; i < 8; i++) {
                if (changed & (1 << i)) {
                    int active = !(sr595.output & (1 << i));
                    printf(" %s=%s", sr_bit_names[i],
                           active ? "ON" : "off");
                }
            }
            printf("\n");

            /* React to floppy signals (bit 4=MOTOR, bit 6=STEP, bit 5=DIR) */
            int motor = !(sr595.output & (1 << 4));
            if (motor && !floppy.motor_on) {
                floppy.motor_on = 1;
                printf("[FLOPPY] Motor ON\n");
            } else if (!motor && floppy.motor_on) {
                floppy.motor_on = 0;
                printf("[FLOPPY] Motor OFF\n");
            }

            /* Step pulse (active low) */
            int step = !(sr595.output & (1 << 6));
            if (step) {
                int dir = !(sr595.output & (1 << 5));
                floppy.track += dir ? 1 : -1;
                if (floppy.track < 0) floppy.track = 0;
                if (floppy.track > 79) floppy.track = 79;
                floppy.step_count++;
                printf("[FLOPPY] Step %s → track %d\n",
                       dir ? "IN" : "OUT", floppy.track);

                /* Update /TRK00 on PC0 */
                if (floppy.track == 0) {
                    /* Assert /TRK00 (drive LOW) */
                    avr_raise_irq(
                        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0),
                        0);
                } else {
                    avr_raise_irq(
                        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('C'), 0),
                        1);
                }
            }
        }
    }
    sr595.rclk_prev = rclk;
}

/* ── Interrupt handler ─────────────────────────────────────────────── */

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

/* ── Print simulation summary ──────────────────────────────────────── */

static void print_summary(void)
{
    uint64_t cycles = avr->cycle - boot_cycle;
    double ms = (double)cycles / (16000000.0 / 1000.0);

    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║         64korppu Simulation Summary          ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ Simulated cycles:  %12lu               ║\n", (unsigned long)cycles);
    printf("║ Simulated time:    %10.1f ms             ║\n", ms);
    printf("║                                              ║\n");
    printf("║ SRAM reads:        %12u               ║\n", sram.total_reads);
    printf("║ SRAM writes:       %12u               ║\n", sram.total_writes);
    printf("║ SRAM mode:         0x%02X (%s)       ║\n", sram.mode,
           sram.mode == 0x40 ? "sequential" : sram.mode == 0x80 ? "page" : "byte      ");
    printf("║                                              ║\n");
    printf("║ 74HC595 latches:   %12u               ║\n", sr595.latch_count);
    printf("║ 74HC595 output:    0x%02X                      ║\n", sr595.output);
    for (int i = 0; i < 8; i++) {
        int active = !(sr595.output & (1 << i));
        if (active)
            printf("║   %-10s       ASSERTED (active)          ║\n",
                   sr_bit_names[i]);
    }
    printf("║                                              ║\n");
    printf("║ Floppy track:      %3d                       ║\n", floppy.track);
    printf("║ Floppy steps:      %12u               ║\n", floppy.step_count);
    printf("║ Floppy motor:      %s                       ║\n",
           floppy.motor_on ? "ON " : "OFF");
    printf("╚══════════════════════════════════════════════╝\n");
}

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <firmware.elf> [-v] [-t <ms>]\n", argv[0]);
        fprintf(stderr, "  -v       Verbose (show SPI transactions)\n");
        fprintf(stderr, "  -t <ms>  Run for <ms> milliseconds (default: 1000)\n");
        return 1;
    }

    const char *elf_path = argv[1];
    int sim_ms = 1000;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) verbose = 1;
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc)
            sim_ms = atoi(argv[++i]);
    }

    /* Load firmware ELF */
    elf_firmware_t fw = {};
    if (elf_read_firmware(elf_path, &fw) != 0) {
        fprintf(stderr, "Error: Cannot read ELF: %s\n", elf_path);
        return 1;
    }

    /* Override MCU if not set in ELF */
    if (fw.mmcu[0] == 0) {
        strcpy(fw.mmcu, "atmega328p");
    }
    if (fw.frequency == 0) {
        fw.frequency = 16000000;
    }

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║       64korppu SimAVR Simulation             ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║ MCU:        %s                     ║\n", fw.mmcu);
    printf("║ Frequency:  %lu MHz                       ║\n",
           fw.frequency / 1000000UL);
    printf("║ Flash:      %u bytes                     ║\n", fw.flashsize);
    printf("║ Duration:   %d ms                         ║\n", sim_ms);
    printf("║ Verbose:    %s                             ║\n",
           verbose ? "yes" : "no ");
    printf("╚══════════════════════════════════════════════╝\n");
    printf("\n--- Firmware UART output ---\n");

    /* Create AVR instance */
    avr = avr_make_mcu_by_name(fw.mmcu);
    if (!avr) {
        fprintf(stderr, "Error: Unknown MCU: %s\n", fw.mmcu);
        return 1;
    }

    avr_init(avr);
    avr_load_firmware(avr, &fw);

    /* Initialize virtual peripherals */
    memset(&sram, 0, sizeof(sram));
    memset(&sr595, 0, sizeof(sr595));
    sr595.output = 0xFF;    /* SR_DEFAULT */
    memset(&floppy, 0, sizeof(floppy));
    floppy.disk_inserted = 1;
    floppy.track = 40;      /* Start at unknown position */

    /* Set floppy input pins: /TRK00=HIGH, /WPT=HIGH, /DSKCHG=HIGH */
    /* (deasserted = no track0, no write-protect, no disk-change) */

    /* Hook UART output */
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_UART_GETIRQ('0'), UART_IRQ_OUTPUT),
        uart_output_hook, NULL);

    /* Hook SPI output (byte sent by AVR) */
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT),
        spi_output_hook, NULL);

    /* Hook Port B output (for /CS_SRAM on PB2) */
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), IOPORT_IRQ_PIN_ALL),
        portb_hook, NULL);

    /* Hook Port D output (for RCLK on PD6) */
    avr_irq_register_notify(
        avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('D'), IOPORT_IRQ_PIN_ALL),
        portd_hook, NULL);

    /* Install signal handler */
    signal(SIGINT, sigint_handler);

    /* Run simulation */
    boot_cycle = avr->cycle;
    uint64_t target_cycles = (uint64_t)sim_ms * (fw.frequency / 1000);
    uint64_t end_cycle = boot_cycle + target_cycles;

    while (running && avr->cycle < end_cycle) {
        int state = avr_run(avr);
        if (state == cpu_Done || state == cpu_Crashed) {
            printf("\n[SIM] AVR %s at cycle %lu\n",
                   state == cpu_Done ? "halted" : "CRASHED",
                   (unsigned long)(avr->cycle - boot_cycle));
            break;
        }
    }

    printf("\n--- End of simulation ---\n");
    print_summary();

    return 0;
}
