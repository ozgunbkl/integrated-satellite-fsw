/* Actual consumer task: a dequeued record is attempted once, even if storage refuses. */
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "freertos/queue.h"
#include "satellite_types.h"
#include "system_mode.h"
#include "watchdog.h"
#include "archive_service.h"
#include "payload_service.h"

void vDataLoggerTask(void *argument);
extern DownlinkMode_t g_downlink_mode;
static int queue_slot;
QueueHandle_t xTelemetryQueue = &queue_slot;
static jmp_buf task_exit;
static unsigned receives, writes, payload_updates, pets;
static HK_Telemetry_t delivered;
static const ArchiveStatus_t results[] = {ARCHIVE_OK, ARCHIVE_ERR_FULL, ARCHIVE_ERR_UNAVAILABLE, ARCHIVE_OK};
int __wrap_printf(const char *format, ...) { return 0; }
BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait) {
    assert(queue == xTelemetryQueue && wait == pdMS_TO_TICKS(100));
    assert(pets == receives && payload_updates == receives);
    ++receives;
    if ((receives % 2) == 0) return pdFAIL; // Timeout leaves no valid item to write.
    memset(&delivered, 0, sizeof(delivered));
    delivered.timestamp = receives; delivered.sequence_count = (uint16_t)receives;
    delivered.bus_voltage = 3.3f;
    memcpy(data, &delivered, sizeof(delivered));
    return pdPASS;
}
ArchiveStatus_t Archive_WriteRecord(uint16_t id, const uint8_t *data, uint16_t length) {
    assert(writes < 4 && id == REC_ID_HK && length == sizeof(delivered));
    assert(memcmp(data, &delivered, length) == 0);
    assert(receives == writes * 2 + 1); // A failed append must not retry the dequeued item.
    return results[writes++];
}
ArchiveStatus_t Archive_ReadRecord(uint16_t index, ArchiveRecord_t *record) {
    assert(!"Inactive readout must not access archive"); return ARCHIVE_ERR_NOT_FOUND;
}
SystemMode_t SYSTEM_MODE_Get(void) { return MODE_NOMINAL; }
void Payload_Update(void) { ++payload_updates; }
void watchdog_pet(WatchdogTaskID_t id) {
    assert(id == WDT_TASK_DATA_LOG); ++pets;
    assert(payload_updates == pets && receives == pets);
    if (pets == 8) longjmp(task_exit, 1);
}
int main(void) {
    assert(g_downlink_mode == DOWNLINK_INACTIVE);
    if (setjmp(task_exit) == 0) vDataLoggerTask(NULL);
    assert(receives == 8 && writes == 4 && payload_updates == 8 && pets == 8);
    puts("PASS: logger bounded queue receive, exact bytes, success/full/unavailable once-only "
         "append, timeout skip, payload and watchdog progress after failure.");
    return 0;
}
