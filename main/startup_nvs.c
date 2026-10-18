#include "startup_nvs.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include <stdbool.h>

static const char *TAG = "MAIN";
static bool s_attempted;
static esp_err_t s_result;

esp_err_t STARTUP_InitNvs(void) {
    // app_main/recovery first, optional Wi-Fi second: one SDK attempt per boot.
    if (s_attempted) return s_result;
    s_attempted = true;
    const esp_err_t result = nvs_flash_init();
    s_result = result;
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "NVS initialization failed (%s); Wi-Fi skipped; automatic NVS erase disabled.",
                 esp_err_to_name(result));
    }
    return result;
}
