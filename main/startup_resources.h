#ifndef STARTUP_RESOURCES_H
#define STARTUP_RESOURCES_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#define STARTUP_TELEMETRY_QUEUE_DEPTH 10U
#define STARTUP_COMMAND_QUEUE_DEPTH 5U

typedef struct {
    QueueHandle_t telemetry_queue;
    QueueHandle_t command_queue;
    SemaphoreHandle_t eps_data_mutex;
} StartupResources_t;

// Call once at boot, before Wi-Fi/log or mission tasks. Returns only on success.
// Allocation failure is fatal (abort), not a partial startup or recovery path.
// Earlier allocations remain until the configured panic/reboot; no retry/cleanup.
// A repeated attempt (including after failure) aborts before touching the output
// or allocating anything. Single caller only; not safe for concurrent calls.
void STARTUP_CreateRequiredResources(StartupResources_t *resources);

#endif
