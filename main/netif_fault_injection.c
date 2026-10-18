/* Explicit target-test profile only; no wrappers in normal firmware. */
#ifdef FSW_TEST_NETIF_INIT_FAILURE
#if defined(FSW_TEST_NVS_INIT_FAILURE) || defined(FSW_TEST_WIFI_TIMEOUT)
#error "Network, NVS and Wi-Fi timeout tests must use separate profiles"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "esp_netif.h"
#include "esp_event.h"
#include "yamcs_interface.h"

esp_err_t __wrap_esp_netif_init(void) {
    static unsigned init_calls = 0;
    if (++init_calls != 1U) {
        printf("TEST ERROR: unexpected network stack init retry.\n");
        abort();
    }
    printf("TEST: Network stack init failure injected (ESP_FAIL); real esp_netif_init not called.\n");
    return ESP_FAIL;
}

esp_err_t __wrap_esp_netif_deinit(void) {
    printf("TEST ERROR: unsupported network stack deinit attempted.\n");
    abort();
}

bool __wrap_YAMCS_MarkNetworkStackReady(void) {
    // Stop before a regression could allow sockets on an uninitialized stack.
    printf("TEST ERROR: readiness published after network stack init failure.\n");
    abort();
}

esp_err_t __wrap_esp_event_loop_create_default(void) {
    printf("TEST ERROR: event loop started after network stack init failure.\n");
    abort();
}
#endif
