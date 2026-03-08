/*
 * IEC Bridge — host build of 64korppu CBM-DOS + D64.
 *
 * Runs the real cbm_dos.c and d64.c code on the host machine,
 * serving IEC commands over a TCP socket. A Python test client
 * (or any TCP client) connects and simulates what a C64 would do.
 *
 * Protocol (binary, big-endian):
 *
 * Client -> Server:
 *   CMD_OPEN    = 0x01, sa(1), len(1), filename(len)
 *   CMD_CLOSE   = 0x02, sa(1)
 *   CMD_READ    = 0x03, sa(1)            // read one byte
 *   CMD_WRITE   = 0x04, sa(1), byte(1)
 *   CMD_EXEC    = 0x05, len(1), cmd(len) // command channel
 *   CMD_QUIT    = 0xFF
 *
 * Server -> Client:
 *   RSP_OK      = 0x00
 *   RSP_BYTE    = 0x01, byte(1), eoi(1)  // eoi: 1=last byte
 *   RSP_NO_DATA = 0x02                   // no byte available
 *   RSP_ERROR   = 0xFF, code(1), len(1), msg(len)
 *
 * Usage:
 *   ./iec_bridge [port] [d64_dir]
 *   Default port: 6464
 *   Default d64_dir: current directory
 *
 * Place .D64 files in d64_dir. They'll be available via
 * CD:FILENAME.D64 command, just like on real hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <ctype.h>

#include "cbm_dos.h"
#include "d64.h"
#include "mock_fat12.h"
#include "mock_iec.h"

#define DEFAULT_PORT 6464

/* Protocol command bytes */
#define CMD_OPEN    0x01
#define CMD_CLOSE   0x02
#define CMD_READ    0x03
#define CMD_WRITE   0x04
#define CMD_EXEC    0x05
#define CMD_QUIT    0xFF

/* Protocol response bytes */
#define RSP_OK      0x00
#define RSP_BYTE    0x01
#define RSP_NO_DATA 0x02
#define RSP_ERROR   0xFF

static char d64_dir[256] = ".";

/*
 * Scan the D64 directory and register all .D64 files in mock FAT12.
 */
static void scan_d64_files(void) {
    mock_fat12_reset();

    DIR *dir = opendir(d64_dir);
    if (!dir) {
        fprintf(stderr, "Cannot open directory: %s\n", d64_dir);
        return;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        int len = strlen(de->d_name);
        if (len < 5) continue;

        /* Check .D64 or .d64 extension */
        const char *ext = &de->d_name[len - 4];
        if (strcasecmp(ext, ".d64") != 0) continue;

        /* Read the file */
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", d64_dir, de->d_name);

        FILE *f = fopen(path, "rb");
        if (!f) continue;

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (size != D64_IMAGE_SIZE) {
            fprintf(stderr, "Skipping %s: wrong size (%ld, expected %d)\n",
                    de->d_name, size, D64_IMAGE_SIZE);
            fclose(f);
            continue;
        }

        uint8_t *data = malloc(D64_IMAGE_SIZE);
        if (!data) { fclose(f); continue; }

        fread(data, 1, D64_IMAGE_SIZE, f);
        fclose(f);

        /* Convert filename to FAT12 8.3 format */
        char name8[9], ext3[4];
        fat12_parse_filename(de->d_name, name8, ext3);

        mock_fat12_add_file(name8, ext3, data, D64_IMAGE_SIZE);
        printf("  Registered: %s\n", de->d_name);
        free(data);
    }

    closedir(dir);
}

static int send_all(int fd, const void *buf, int len) {
    const uint8_t *p = buf;
    while (len > 0) {
        int n = write(fd, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, int len) {
    uint8_t *p = buf;
    while (len > 0) {
        int n = read(fd, p, len);
        if (n <= 0) return -1;
        p += n;
        len -= n;
    }
    return 0;
}

static void send_ok(int fd) {
    uint8_t rsp = RSP_OK;
    send_all(fd, &rsp, 1);
}

static void send_error(int fd) {
    uint8_t code = mock_iec_get_error_code();
    const char *msg = mock_iec_get_error_msg();
    uint8_t mlen = strlen(msg);

    uint8_t hdr[3] = { RSP_ERROR, code, mlen };
    send_all(fd, hdr, 3);
    send_all(fd, msg, mlen);
}

static void handle_client(int client_fd) {
    printf("Client connected.\n");

    /* Initialize CBM-DOS */
    cbm_dos_init();
    scan_d64_files();

    bool running = true;
    while (running) {
        uint8_t cmd;
        if (recv_all(client_fd, &cmd, 1) < 0) break;

        switch (cmd) {
            case CMD_OPEN: {
                uint8_t sa, flen;
                recv_all(client_fd, &sa, 1);
                recv_all(client_fd, &flen, 1);
                char filename[256] = {0};
                if (flen > 0) recv_all(client_fd, filename, flen);
                filename[flen] = '\0';

                printf("  OPEN SA=%d \"%s\"\n", sa, filename);
                cbm_dos_open(sa, filename, flen);

                if (mock_iec_get_error_code() == 0) {
                    send_ok(client_fd);
                } else {
                    send_error(client_fd);
                }
                break;
            }

            case CMD_CLOSE: {
                uint8_t sa;
                recv_all(client_fd, &sa, 1);
                printf("  CLOSE SA=%d\n", sa);
                cbm_dos_close(sa);
                send_ok(client_fd);
                break;
            }

            case CMD_READ: {
                uint8_t sa;
                recv_all(client_fd, &sa, 1);

                uint8_t byte;
                bool eoi;
                bool ok = cbm_dos_talk_byte(sa, &byte, &eoi);

                if (ok) {
                    uint8_t rsp[3] = { RSP_BYTE, byte, eoi ? 1 : 0 };
                    send_all(client_fd, rsp, 3);
                } else {
                    uint8_t rsp = RSP_NO_DATA;
                    send_all(client_fd, &rsp, 1);
                }
                break;
            }

            case CMD_WRITE: {
                uint8_t sa, byte;
                recv_all(client_fd, &sa, 1);
                recv_all(client_fd, &byte, 1);
                cbm_dos_listen_byte(sa, byte);
                send_ok(client_fd);
                break;
            }

            case CMD_EXEC: {
                uint8_t clen;
                recv_all(client_fd, &clen, 1);
                char command[256] = {0};
                if (clen > 0) recv_all(client_fd, command, clen);

                printf("  EXEC \"%.*s\"\n", clen, command);
                cbm_dos_execute_command(command, clen);

                if (mock_iec_get_error_code() == 0 ||
                    mock_iec_get_error_code() == 1) {  /* FILES SCRATCHED is OK */
                    send_ok(client_fd);
                } else {
                    send_error(client_fd);
                }
                break;
            }

            case CMD_QUIT:
                printf("  QUIT\n");
                running = false;
                send_ok(client_fd);
                break;

            default:
                fprintf(stderr, "  Unknown command: 0x%02X\n", cmd);
                running = false;
                break;
        }
    }

    /* Clean up */
    if (d64_is_mounted()) {
        d64_unmount();
    }

    close(client_fd);
    printf("Client disconnected.\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc > 1) port = atoi(argv[1]);
    if (argc > 2) strncpy(d64_dir, argv[2], sizeof(d64_dir) - 1);

    printf("64korppu IEC Bridge\n");
    printf("  Port: %d\n", port);
    printf("  D64 dir: %s\n", d64_dir);
    printf("  Scanning for .D64 files...\n");
    scan_d64_files();

    /* Create TCP server socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port),
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen"); return 1;
    }

    printf("Listening on port %d... (Ctrl+C to quit)\n\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        handle_client(client_fd);

        /* Re-scan files for next connection */
        scan_d64_files();
    }

    close(server_fd);
    return 0;
}
