#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Optional, boot-only diagnostics. Mission queues must remain alive for boot.
// No restart/reset, allocation stress, queue consumption or control commands.
// Creation failure leaves mission tasks running; caller reports diagnostics absent.
bool RESOURCE_MONITOR_Start(QueueHandle_t telemetry, QueueHandle_t commands);

#endif
