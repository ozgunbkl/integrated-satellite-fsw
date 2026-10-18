#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "wifi_mock.h"
#include "freertos/queue.h"
#include "yamcs_log.h"
#include "yamcs_interface.h"

static bool scheduler, fail_queue, fail_task, link_connected, full;
static unsigned queue_creates, task_creates, queue_deletes, enqueues, receives, sends;
static char captured[256];
static TaskFunction_t sender;
static jmp_buf task_exit;
BaseType_t xTaskGetSchedulerState(void) { return scheduler ? taskSCHEDULER_RUNNING : 0; }
QueueHandle_t xQueueCreate(unsigned depth, unsigned size) {
    assert(depth == 32 && size == 256); queue_creates++;
    if (fail_queue) { fail_queue = false; return NULL; }
    return (void *)1;
}
void vQueueDelete(QueueHandle_t queue) { assert(queue == (void *)1); queue_deletes++; }
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t depth, void *arg, unsigned priority, TaskHandle_t *handle) {
    assert(strcmp(name, "YLOG_TX") == 0 && depth == 3072 && priority == 1 && arg == NULL && handle == NULL);
    task_creates++; sender = fn;
    if (fail_task) { fail_task = false; return pdFALSE; }
    return pdPASS;
}
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait) {
    assert(queue == (void *)1 && wait == 0); enqueues++;
    memcpy(captured, item, 256);
    return full ? pdFALSE : pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait) {
    assert(queue == (void *)1 && wait == portMAX_DELAY);
    if (receives++ == 2) { longjmp(task_exit, 1); }
    link_connected = receives == 2;
    memset(item, 0, 256); memcpy(item, "queued test", 11);
    return pdTRUE;
}
bool YAMCS_IsLinkConnected(void) { return link_connected; }
bool YAMCS_SendEvent(const char *format, ...) {
    assert(link_connected); sends++;
    char message[256]; va_list args; va_start(args, format);
    vsnprintf(message, sizeof(message), format, args); va_end(args);
    assert(strcmp(message, "INFO: queued test") == 0);
    return true;
}
int main(void) {
    assert(!YAMCS_LogInit() && queue_creates == 0);
    scheduler = true; fail_queue = true;
    assert(!YAMCS_LogInit() && queue_creates == 1 && task_creates == 0);
    fail_task = true;
    assert(!YAMCS_LogInit() && queue_creates == 2 && task_creates == 1 && queue_deletes == 1);
    assert(YAMCS_LogInit() && queue_creates == 3 && task_creates == 2);
    assert(YAMCS_LogInit() && queue_creates == 3 && task_creates == 2);
    YAMCS_LogPrintf("offline message\n"); assert(enqueues == 0);
    link_connected = true;
    YAMCS_LogPrintf("partial"); assert(enqueues == 0);
    YAMCS_LogPrintf("I (123) SDK message\n"); assert(enqueues == 0);
    YAMCS_LogPrintf("online message %u\n", 7u);
    assert(enqueues == 1 && strcmp(captured, "online message 7") == 0);
    full = true; YAMCS_LogPrintf("queue full\n"); assert(enqueues == 2);
    assert(YAMCS_LogPrintf(NULL) == 0);
    if (setjmp(task_exit) == 0) { sender(NULL); assert(false); }
    assert(sends == 1); // Offline queued item dropped; online item sent once.
    puts("PASS: log allocation/task failures retry safely, one persistent sender, offline/full drops and no recursive mirror.");
    return 0;
}
