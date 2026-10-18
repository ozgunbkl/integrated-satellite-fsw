#include "link_retry.h"

static bool due(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

void LINK_Init(LinkRetry *state, uint32_t now) {
    *state = (LinkRetry){.next_ms = now, .backoff_ms = 1000};
}

void LINK_Failed(LinkRetry *state, uint32_t now) {
    state->pending = false;
    state->next_ms = now + state->backoff_ms;
    state->backoff_ms = state->backoff_ms < 15000 ? state->backoff_ms * 2 : 30000;
}

LinkAction LINK_Step(LinkRetry *state, uint32_t now, bool started,
                     bool connected, bool disconnected) {
    if (connected) {
        state->was_connected = true;
        state->pending = false;
        state->backoff_ms = 1000;
        return LINK_IDLE;
    }
    if (state->was_connected) {
        state->was_connected = false;
        LINK_Failed(state, now);
    } else if (disconnected && state->pending) {
        LINK_Failed(state, now);
    }
    if (!started) { return LINK_IDLE; }
    // Recover even if the driver never emits a disconnect after connect().
    if (state->pending && due(now, state->attempt_ms + 20000)) {
        LINK_Failed(state, now);
        return LINK_DISCONNECT;
    }
    if (!state->pending && due(now, state->next_ms)) {
        state->pending = true;
        state->attempt_ms = now;
        state->attempts++;
        return LINK_CONNECT;
    }
    return LINK_IDLE;
}
