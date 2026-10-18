#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H
#include <stdbool.h>
#include "esp_err.h"

// Optional startup service, called once after YAMCS_Init()/mandatory resources.
// Timeout leaves a live reconnect manager; other failures disable Wi-Fi safely.
// No full SDK/network-stack retry or adoption of another owner's interface.
esp_err_t WIFI_Start(void);
bool WIFI_IsConnected(void);
bool WIFI_IsLogMirrorStarted(void);
#endif
