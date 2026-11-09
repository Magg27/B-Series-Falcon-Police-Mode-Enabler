#define _POSIX_C_SOURCE 200809L
#include "pme.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

static int64_t now_ms(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0;
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static void set_message(char *message, size_t size, const char *value)
{
    if (size) snprintf(message, size, "%s", value);
}

static int exchange(PmeTransport *transport, const char *command, char *reply,
                    size_t capacity, int timeout_ms)
{
    uint8_t request[32];
    size_t length = strlen(command);
    if (length + 1 > sizeof(request) || capacity < 2) return -1;
    memcpy(request, command, length);
    request[length++] = '\r';
    int64_t deadline = now_ms() + timeout_ms;
    size_t offset = 0;
    while (offset < length) {
        int remaining = (int)(deadline - now_ms());
        if (remaining <= 0) return -2;
        int sent = transport->write(transport->context, request + offset,
                                    length - offset, remaining);
        if (sent <= 0 || (size_t)sent > length - offset) return -3;
        offset += (size_t)sent;
    }
    size_t used = 0;
    reply[0] = 0;
    while (now_ms() < deadline) {
        uint8_t bytes[256];
        int remaining = (int)(deadline - now_ms());
        int received = transport->read(transport->context, bytes, sizeof(bytes),
                                        remaining > 250 ? 250 : remaining);
        if (received < 0 || received > (int)sizeof(bytes)) return -3;
        for (int i = 0; i < received; i++) {
            if (used + 1 >= capacity) return -4;
            reply[used++] = (char)bytes[i];
            reply[used] = 0;
            if (bytes[i] == '>') return 0;
        }
    }
    return -2;
}

static int reply_contains(const char *reply, const char *word)
{
    size_t length = strlen(word);
    for (; *reply; reply++) if (strncasecmp(reply, word, length) == 0) return 1;
    return 0;
}

static int frame(const char *text, const uint8_t *prefix, size_t prefix_length,
                 uint8_t *data, size_t required_size)
{
    int matches = 0;
    while (*text) {
        const char *end = text;
        while (*end && *end != '\r' && *end != '\n') end++;
        size_t line_length = (size_t)(end - text);
        if (line_length && line_length < 128) {
            char line[128];
            memcpy(line, text, line_length);
            line[line_length] = 0;
            char *save = NULL;
            char *part = strtok_r(line, " \t", &save);
            if (part && strcasecmp(part, "728") == 0) {
                uint8_t values[9];
                size_t count = 0;
                int valid = 1;
                while ((part = strtok_r(NULL, " \t", &save)) != NULL) {
                    if (count >= sizeof(values) || strlen(part) != 2 ||
                        !isxdigit((unsigned char)part[0]) ||
                        !isxdigit((unsigned char)part[1])) {
                        valid = 0;
                        break;
                    }
                    values[count++] = (uint8_t)strtoul(part, NULL, 16);
                }
                if (valid && count == required_size + 1 &&
                    values[0] == required_size &&
                    memcmp(values + 1, prefix, prefix_length) == 0) {
                    memcpy(data, values + 1, required_size);
                    matches++;
                }
            }
        }
        text = end;
        while (*text == '\r' || *text == '\n') text++;
    }
    return matches == 1;
}

static int prepare(PmeTransport *transport, char *message, size_t size)
{
    char reply[4096];
    if (exchange(transport, "ATZ", reply, sizeof(reply), 6000) != 0 ||
        !reply_contains(reply, "ELM")) {
        set_message(message, size, "ELM327 reset failed or adapter was not recognised.");
        return -1;
    }
    const char *commands[] = {
        "ATE0", "ATL0", "ATS1", "ATCAF1", "ATSPA6", "ATSH720", "ATH1"
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (exchange(transport, commands[i], reply, sizeof(reply), 3500) != 0 ||
            !reply_contains(reply, "OK")) {
            snprintf(message, size, "Adapter setup failed at %s.", commands[i]);
            return -1;
        }
    }
    return 0;
}

static int read_state(PmeTransport *transport, PmeState *state,
                      char *message, size_t size)
{
    char reply[4096];
    uint8_t payload[8];
    const uint8_t version_prefix[] = {0x62, 0xE2, 0x1A};
    const uint8_t config_prefix[] = {0x61, 0x03};
    memset(state, 0, sizeof(*state));
    if (exchange(transport, "22E21A", reply, sizeof(reply), 7000) != 0 ||
        !frame(reply, version_prefix, sizeof(version_prefix), payload, 7)) {
        set_message(message, size, "Firmware identification failed (expected CAN 728 / 62 E2 1A).");
        return -1;
    }
    for (int i = 0; i < 4; i++) {
        if (!isprint((unsigned char)payload[i + 3])) {
            set_message(message, size, "The firmware ID contains invalid characters.");
            return -1;
        }
        state->firmware[i] = (char)payload[i + 3];
    }
    state->verified_model = strcmp(state->firmware, "5R29") == 0;
    if (exchange(transport, "2103", reply, sizeof(reply), 7000) != 0 ||
        !frame(reply, config_prefix, sizeof(config_prefix), payload, 4)) {
        set_message(message, size, "Configuration read failed (expected CAN 728 / 61 03).");
        return -1;
    }
    state->flags = payload[2];
    state->options = payload[3];
    state->police_mode = (payload[2] & 0x04) != 0;
    return 0;
}

int pme_run(PmeTransport *transport, int operation, int allow_untested,
            PmeState *state, char *message, size_t message_size)
{
    if (!transport || !transport->read || !transport->write || !state ||
        !message || !message_size || operation < PME_READ || operation > PME_DISABLE) return -1;
    set_message(message, message_size, "Communication failed.");
    if (prepare(transport, message, message_size) != 0) return -1;
    if (read_state(transport, state, message, message_size) != 0) return -1;
    if (operation == PME_READ) {
        snprintf(message, message_size, "Firmware %s | Flags %02X %02X | Police Mode %s | %s",
                 state->firmware, state->flags, state->options,
                 state->police_mode ? "ON" : "OFF",
                 state->verified_model ? "BF 5R29 tested" : "UNVERIFIED firmware");
        return 0;
    }
    int enable = operation == PME_ENABLE;
    if ((state->police_mode != 0) == enable) {
        snprintf(message, message_size, "Police Mode is already %s. No write sent.",
                 enable ? "enabled" : "disabled");
        return 0;
    }
    if (!allow_untested && (!state->verified_model || state->options != 0x0C ||
                             (state->flags & (uint8_t)~0x04) != 0x01)) {
        snprintf(message, message_size,
                 "Write blocked: %s / %02X %02X is not the verified BF 5R29 configuration."
                 " Enable the unverified-model option only after checking compatibility.",
                 state->firmware, state->flags, state->options);
        return -1;
    }
    char original_firmware[5];
    memcpy(original_firmware, state->firmware, sizeof(original_firmware));
    uint8_t original_options = state->options;
    uint8_t expected = enable ? (uint8_t)(state->flags | 0x04) :
                                (uint8_t)(state->flags & (uint8_t)~0x04);
    char command[16];
    snprintf(command, sizeof(command), "3B03%02X%02X", expected, state->options);
    char reply[4096];
    uint8_t ack[8];
    const uint8_t ack_prefix[] = {0x7B, 0x03};
    if (exchange(transport, command, reply, sizeof(reply), 7000) != 0) {
        set_message(message, message_size, "Write response missing. Outcome unknown. Do not retry blindly; read again.");
        return -1;
    }
    if (!frame(reply, ack_prefix, sizeof(ack_prefix), ack, 2)) {
        set_message(message, message_size, "No valid write acknowledgement. Outcome unknown; read again.");
        return -1;
    }
    PmeState checked;
    if (read_state(transport, &checked, message, message_size) != 0) {
        set_message(message, message_size, "Write acknowledged, but readback failed. State unknown; read again.");
        return -1;
    }
    *state = checked;
    if (strcmp(state->firmware, original_firmware) != 0 ||
        state->flags != expected || state->options != original_options ||
        (state->police_mode != 0) != enable) {
        set_message(message, message_size, "Write acknowledged, but verification differs. Read again before taking action.");
        return -1;
    }
    snprintf(message, message_size, "Verified: Police Mode %s | Firmware %s | Config %02X %02X",
             enable ? "ENABLED" : "DISABLED", state->firmware, state->flags, state->options);
    return 0;
}
