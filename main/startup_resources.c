#include "startup_resources.h"
#include "satellite_types.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stdlib.h>

#define TELEMETRY_QUEUE_DEPTH STARTUP_TELEMETRY_QUEUE_DEPTH
#define COMMAND_QUEUE_DEPTH STARTUP_COMMAND_QUEUE_DEPTH

static bool s_creation_attempted = false;

void STARTUP_CreateRequiredResources(StartupResources_t *resources) {
    // Single startup caller only, not a concurrent or runtime recovery API.
    // Latch even failed attempts: never overwrite handles or retry allocations.
    if (s_creation_attempted) {
        ESP_LOGE("MAIN", "Required resource creation already attempted; refusing repeated creation.");
        abort();
    }
    s_creation_attempted = true;

    if (resources == NULL) {
        ESP_LOGE("MAIN", "Required resource output is NULL");
        abort();
    }
    *resources = (StartupResources_t){0};

    resources->telemetry_queue = xQueueCreate(TELEMETRY_QUEUE_DEPTH, sizeof(HK_Telemetry_t));
    if (resources->telemetry_queue == NULL) {
        ESP_LOGE("MAIN", "Required telemetry queue creation failed (depth=%u, item_size=%u)",
                 (unsigned)TELEMETRY_QUEUE_DEPTH, (unsigned)sizeof(HK_Telemetry_t));
        abort();
    }

    resources->command_queue = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(TelecommandPacket_t));
    if (resources->command_queue == NULL) {
        ESP_LOGE("MAIN", "Required command queue creation failed (depth=%u, item_size=%u)",
                 (unsigned)COMMAND_QUEUE_DEPTH, (unsigned)sizeof(TelecommandPacket_t));
        abort();
    }

    resources->eps_data_mutex = xSemaphoreCreateMutex();
    if (resources->eps_data_mutex == NULL) {
        ESP_LOGE("MAIN", "Required EPS data mutex creation failed");
        abort();
    }
}
