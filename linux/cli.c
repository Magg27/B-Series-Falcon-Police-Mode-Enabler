#define _POSIX_C_SOURCE 200809L
#include "pme.h"
#include "serial.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --port /dev/ttyUSB1 [--baud 38400] [--read|--enable|--disable] [--allow-untested]\n"
                    "       %s --list\n", program, program);
}

int main(int argc, char **argv)
{
    char device[256] = "";
    int baud = 38400;
    int operation = PME_READ;
    int untested = 0;
    int selected = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            char ports[64][256];
            int count = serial_ports(ports, 64);
            if (!count) puts("No serial ports found.");
            for (int j = 0; j < count; j++) puts(ports[j]);
            return 0;
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            snprintf(device, sizeof(device), "%s", argv[++i]);
        } else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc) {
            baud = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--read") == 0) {
            operation = PME_READ;
            selected++;
        } else if (strcmp(argv[i], "--enable") == 0) {
            operation = PME_ENABLE;
            selected++;
        } else if (strcmp(argv[i], "--disable") == 0) {
            operation = PME_DISABLE;
            selected++;
        } else if (strcmp(argv[i], "--allow-untested") == 0) {
            untested = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!*device || selected > 1) { usage(argv[0]); return 2; }
    if (operation != PME_READ) {
        if (!isatty(STDIN_FILENO)) {
            fputs("Refusing configuration changes without an interactive terminal.\n", stderr);
            return 2;
        }
        const char *word = operation == PME_ENABLE ? "ENABLE" : "DISABLE";
        printf("This will change the cluster configuration. Type %s to confirm: ", word);
        fflush(stdout);
        char answer[64];
        if (!fgets(answer, sizeof(answer), stdin)) {
            fputs("Cancelled.\n", stderr);
            return 2;
        }
        answer[strcspn(answer, "\r\n")] = 0;
        if (strcmp(answer, word) != 0) {
            fputs("Cancelled.\n", stderr);
            return 2;
        }
    }
    int fd = serial_open(device, baud);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", device, strerror(errno));
        return 1;
    }
    PmeTransport transport = serial_transport(&fd);
    PmeState state;
    char message[512];
    int result = pme_run(&transport, operation, untested, &state, message, sizeof(message));
    serial_close(fd);
    puts(message);
    return result == 0 ? 0 : 1;
}
