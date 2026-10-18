/* Explicit target-test profile only; normal firmware contains no wrappers. */
#ifdef FSW_TEST_NVS_INIT_FAILURE
#ifdef FSW_TEST_WIFI_TIMEOUT
#error "NVS failure and Wi-Fi timeout tests must use separate profiles"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "nvs_flash.h"

esp_err_t __wrap_nvs_flash_init(void) {
    static unsigned init_calls = 0;
    if (++init_calls != 1U) {
        printf("TEST ERROR: unexpected NVS init retry.\n");
        abort();
    }
    // Do not call __real_nvs_flash_init: no real storage corruption is needed.
    printf("TEST: NVS init failure injected (ESP_ERR_NVS_NO_FREE_PAGES); real NVS init not called.\n");
    return ESP_ERR_NVS_NO_FREE_PAGES;
}

esp_err_t __wrap_nvs_flash_erase(void) {
    // Fail before an accidental regression could erase the real NVS partition.
    printf("TEST ERROR: forbidden NVS partition erase attempted.\n");
    abort();
}
#endif
