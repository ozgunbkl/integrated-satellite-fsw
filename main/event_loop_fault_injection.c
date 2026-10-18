/* Explicit target-test profile; normal firmware contains no fault wrappers. */
#ifdef FSW_TEST_EVENT_LOOP_FAILURE
#if defined(FSW_TEST_NVS_INIT_FAILURE) || defined(FSW_TEST_NETIF_INIT_FAILURE) || defined(FSW_TEST_WIFI_TIMEOUT)
#error "Startup failure and Wi-Fi timeout tests must use separate profiles"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

esp_err_t __wrap_esp_event_loop_create_default(void) {
    static unsigned create_calls = 0;
    if (++create_calls != 1U) {
        printf("TEST ERROR: unexpected event loop creation retry.\n");
        abort();
    }
    printf("TEST: Wi-Fi event loop failure injected (ESP_ERR_NO_MEM); real event loop creation not called.\n");
    return ESP_ERR_NO_MEM;
}

esp_err_t __wrap_esp_event_loop_delete_default(void) {
    printf("TEST ERROR: event loop deleted after failed creation.\n");
    abort();
}

esp_netif_t *__wrap_esp_netif_create_default_wifi_sta(void) {
    printf("TEST ERROR: Wi-Fi interface created after event loop failure.\n");
    abort();
}

esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    printf("TEST ERROR: Wi-Fi driver started after event loop failure.\n");
    abort();
}
#endif
