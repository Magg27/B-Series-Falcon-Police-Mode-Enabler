#include "pme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char request[40];
    char response[256];
    size_t offset;
    char firmware[5];
    unsigned flags;
    unsigned options;
    int writes;
    int invalid_reply;
    int missing_ack;
} Simulator;

static int send_data(void *context, const uint8_t *data, size_t size, int timeout_ms)
{
    Simulator *sim = context;
    (void)timeout_ms;
    if (!size || size >= sizeof(sim->request)) return -1;
    memcpy(sim->request, data, size);
    sim->request[size] = 0;
    for (size_t i = 0; i < size; i++) if (sim->request[i] == '\r') sim->request[i] = 0;
    sim->offset = 0;
    const char *command = sim->request;
    if (strcmp(command, "ATZ") == 0) {
        snprintf(sim->response, sizeof(sim->response), "ELM327 v1.5\r\r>");
    } else if (command[0] == 'A' && command[1] == 'T') {
        snprintf(sim->response, sizeof(sim->response), "OK\r>");
    } else if (strcmp(command, "22E21A") == 0) {
        snprintf(sim->response, sizeof(sim->response), "728 07 62 E2 1A %02X %02X %02X %02X\r>",
                 (unsigned char)sim->firmware[0], (unsigned char)sim->firmware[1],
                 (unsigned char)sim->firmware[2], (unsigned char)sim->firmware[3]);
    } else if (strcmp(command, "2103") == 0) {
        if (sim->invalid_reply) snprintf(sim->response, sizeof(sim->response), "728 05 61 03 %02X %02X\r>", sim->flags, sim->options);
        else snprintf(sim->response, sizeof(sim->response), "728 04 61 03 %02X %02X\r>", sim->flags, sim->options);
    } else if (strncmp(command, "3B03", 4) == 0 && strlen(command) == 8) {
        sim->writes++;
        if (sim->missing_ack) snprintf(sim->response, sizeof(sim->response), "728 03 7F 3B 31\r>");
        else {
            unsigned flags = 0, options = 0;
            sscanf(command + 4, "%02X%02X", &flags, &options);
            sim->flags = flags;
            sim->options = options;
            snprintf(sim->response, sizeof(sim->response), "728 02 7B 03\r>");
        }
    } else return -1;
    return (int)size;
}

static int receive_data(void *context, uint8_t *data, size_t size, int timeout_ms)
{
    Simulator *sim = context;
    (void)timeout_ms;
    size_t remaining = strlen(sim->response) - sim->offset;
    if (!remaining) return 0;
    if (remaining > size) remaining = size;
    memcpy(data, sim->response + sim->offset, remaining);
    sim->offset += remaining;
    return (int)remaining;
}

static void check(int result, const char *name)
{
    if (!result) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
    printf("PASS: %s\n", name);
}

int main(void)
{
    Simulator sim = {.firmware = "5R29", .flags = 1, .options = 12};
    PmeTransport port = {.context = &sim, .write = send_data, .read = receive_data};
    PmeState state;
    char message[256];
    check(pme_run(&port, PME_READ, 0, &state, message, sizeof(message)) == 0 &&
          state.flags == 1 && !state.police_mode && sim.writes == 0, "read off, no write");
    check(pme_run(&port, PME_ENABLE, 0, &state, message, sizeof(message)) == 0 &&
          state.police_mode && state.flags == 5 && sim.writes == 1, "enable and readback");
    check(pme_run(&port, PME_ENABLE, 0, &state, message, sizeof(message)) == 0 && sim.writes == 1,
          "already enabled, no write");
    check(pme_run(&port, PME_DISABLE, 0, &state, message, sizeof(message)) == 0 &&
          state.flags == 1 && sim.writes == 2, "disable and readback");
    strcpy(sim.firmware, "6A22");
    check(pme_run(&port, PME_ENABLE, 0, &state, message, sizeof(message)) != 0 &&
          sim.writes == 2, "untested firmware blocked");
    check(pme_run(&port, PME_ENABLE, 1, &state, message, sizeof(message)) == 0 &&
          sim.flags == 5 && sim.writes == 3, "explicit untested firmware override");
    sim.invalid_reply = 1;
    check(pme_run(&port, PME_READ, 0, &state, message, sizeof(message)) != 0,
          "malformed response rejected");
    sim.invalid_reply = 0;
    strcpy(sim.firmware, "5R29");
    sim.flags = 1;
    sim.missing_ack = 1;
    check(pme_run(&port, PME_ENABLE, 0, &state, message, sizeof(message)) != 0 &&
          sim.writes == 4, "write not acknowledged; no retries");
    sim.missing_ack = 0;
    sim.flags = 9;
    check(pme_run(&port, PME_ENABLE, 0, &state, message, sizeof(message)) != 0 &&
          sim.writes == 4, "unexpected starting configuration blocked");
    strcpy(sim.firmware, "6A22");
    sim.flags = 0x21;
    sim.options = 0x0D;
    check(pme_run(&port, PME_ENABLE, 1, &state, message, sizeof(message)) == 0 &&
          state.flags == 0x25 && state.options == 0x0D,
          "preserve unrelated configuration bits and second byte");
    puts("All protocol tests passed.");
    return 0;
}
