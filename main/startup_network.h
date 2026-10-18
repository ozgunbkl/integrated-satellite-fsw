#ifndef STARTUP_NETWORK_H
#define STARTUP_NETWORK_H

#include "esp_err.h"

// Startup only, after YAMCS_Init() and successful NVS initialization.
// One SDK attempt; publish socket readiness only after ESP_OK. No retry/deinit:
// the SDK does not support full lwIP rollback and may leave partial resources.
// Readiness-publication failure returns ESP_FAIL without aborting the core.
esp_err_t STARTUP_InitNetworkStack(void);

// Startup only, after successful STARTUP_InitNetworkStack(). One SDK attempt.
// Do not delete/recreate/adopt an existing loop; preserve socket readiness.
esp_err_t STARTUP_CreateWifiEventLoop(void);

#endif
