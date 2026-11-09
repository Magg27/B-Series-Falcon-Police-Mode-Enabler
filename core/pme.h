#ifndef PME_H
#define PME_H

#include <stddef.h>
#include <stdint.h>

#define PME_READ 0
#define PME_ENABLE 1
#define PME_DISABLE 2

typedef struct {
    void *context;
    int (*write)(void *context, const uint8_t *data, size_t size, int timeout_ms);
    int (*read)(void *context, uint8_t *data, size_t size, int timeout_ms);
} PmeTransport;

typedef struct {
    char firmware[5];
    uint8_t flags;
    uint8_t options;
    int police_mode;
    int verified_model;
} PmeState;

int pme_run(PmeTransport *transport, int operation, int allow_untested,
            PmeState *state, char *message, size_t message_size);

#endif
