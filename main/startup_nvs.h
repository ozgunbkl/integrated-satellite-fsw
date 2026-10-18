#ifndef STARTUP_NVS_H
#define STARTUP_NVS_H

#include "esp_err.h"

// One startup attempt. Returns the SDK result without full-partition erase,
// retry or abort. Shared recovery/Wi-Fi callers receive the cached first result.
// The SDK's own initialization/recovery may still write flash.
esp_err_t STARTUP_InitNvs(void);

#endif
