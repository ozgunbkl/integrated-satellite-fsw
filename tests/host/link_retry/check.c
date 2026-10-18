#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "link_retry.h"
int main(void) {
    LinkRetry state;
    LINK_Init(&state, 0);
    assert(LINK_Step(&state, 0, false, false, false) == LINK_IDLE);
    assert(LINK_Step(&state, 0, true, false, false) == LINK_CONNECT);
    assert(LINK_Step(&state, 19999, true, false, false) == LINK_IDLE);
    assert(LINK_Step(&state, 20000, true, false, false) == LINK_DISCONNECT);
    assert(LINK_Step(&state, 20999, true, false, false) == LINK_IDLE);
    assert(LINK_Step(&state, 21000, true, false, false) == LINK_CONNECT);
    uint32_t now = 21000;
    for (unsigned i = 0; i < 40; i++) {
        assert(LINK_Step(&state, now, true, false, true) == LINK_IDLE);
        assert(state.backoff_ms <= 30000 && !state.pending);
        now = state.next_ms;
        assert(LINK_Step(&state, now - 1, true, false, false) == LINK_IDLE);
        assert(LINK_Step(&state, now, true, false, false) == LINK_CONNECT);
    }
    assert(state.attempts > 10 && state.backoff_ms == 30000);
    assert(LINK_Step(&state, now, true, true, false) == LINK_IDLE);
    assert(state.backoff_ms == 1000 && !state.pending);
    assert(LINK_Step(&state, now + 1, true, false, true) == LINK_IDLE);
    assert(LINK_Step(&state, now + 1000, true, false, false) == LINK_IDLE);
    assert(LINK_Step(&state, now + 1001, true, false, false) == LINK_CONNECT);
    LINK_Init(&state, UINT32_MAX - 500);
    LINK_Failed(&state, UINT32_MAX - 500);
    assert(LINK_Step(&state, 498, true, false, false) == LINK_IDLE);
    assert(LINK_Step(&state, 499, true, false, false) == LINK_CONNECT);
    puts("PASS: reconnect backoff, >10 failures, pending timeout, recovery and millisecond wrap.");
    return 0;
}
