#ifndef LINK_RETRY_H
#define LINK_RETRY_H
#include <stdbool.h>
#include <stdint.h>

typedef enum { LINK_IDLE, LINK_CONNECT, LINK_DISCONNECT } LinkAction;
typedef struct {
    uint32_t next_ms, attempt_ms, backoff_ms, attempts;
    bool pending, was_connected;
} LinkRetry;

void LINK_Init(LinkRetry *state, uint32_t now);
void LINK_Failed(LinkRetry *state, uint32_t now);
LinkAction LINK_Step(LinkRetry *state, uint32_t now, bool started,
                     bool connected, bool disconnected);
#endif
