#include "startup_network.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "yamcs_interface.h"

static const char *TAG = "MAIN";

esp_err_t STARTUP_InitNetworkStack(void) {
    const esp_err_t result = esp_netif_init();
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "Network stack initialization failed (%s); Wi-Fi skipped; socket access stays disabled.",
                 esp_err_to_name(result));
        return result;
    }

    // Stack readiness is independent of Wi-Fi connectivity.
    if (!YAMCS_MarkNetworkStackReady()) {
        ESP_LOGE(TAG, "Failed to publish network stack readiness; Wi-Fi skipped; socket access stays disabled.");
        return ESP_FAIL;
    }
    printf("MAIN: Network stack initialized; socket access enabled.\n");
    return ESP_OK;
}

esp_err_t STARTUP_CreateWifiEventLoop(void) {
    const esp_err_t result = esp_event_loop_create_default();
    if (result != ESP_OK) {
        ESP_LOGE(TAG,
                 "Wi-Fi event loop initialization failed (%s); Wi-Fi skipped; network stack stays ready.",
                 esp_err_to_name(result));
        return result;
    }
    printf("MAIN: Wi-Fi event loop created before interface/driver initialization.\n");
    return ESP_OK;
}
