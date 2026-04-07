#include "yamcs_log.h"

#include "yamcs_interface.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define YAMCS_LOG_MIRROR_BUF 256U
#define YAMCS_LOG_QUEUE_DEPTH 32U

typedef struct {
    char text[YAMCS_LOG_MIRROR_BUF];
} YAMCS_LogItem_t;

static bool s_mirror_in_progress = false;
static bool s_sender_task_started = false;
static QueueHandle_t s_log_queue = NULL;

static void yamcs_log_sender_task(void *arg) {
    (void)arg;

    YAMCS_LogItem_t item;
    while (1) {
        if (xQueueReceive(s_log_queue, &item, portMAX_DELAY) == pdTRUE) {
            s_mirror_in_progress = true;
            (void)YAMCS_SendEvent("INFO: %s", item.text);
            s_mirror_in_progress = false;
        }
    }
}

static void yamcs_log_start_sender_task_if_needed(void) {
    if (s_sender_task_started) {
        return;
    }

    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        return;
    }

    if (s_log_queue == NULL) {
        s_log_queue = xQueueCreate(YAMCS_LOG_QUEUE_DEPTH, sizeof(YAMCS_LogItem_t));
        if (s_log_queue == NULL) {
            return;
        }
    }

    if (xTaskCreate(yamcs_log_sender_task, "YLOG_TX", 3072, NULL, 1, NULL) == pdPASS) {
        s_sender_task_started = true;
    }
}

static int yamcs_printf_common(const char *fmt, va_list args) {
    va_list args_print;
    va_list args_copy;

    va_copy(args_print, args);
    va_copy(args_copy, args);

    const int printed = vprintf(fmt, args_print);
    va_end(args_print);

    char msg[YAMCS_LOG_MIRROR_BUF];
    const int written = vsnprintf(msg, sizeof(msg), fmt, args_copy);
    va_end(args_copy);

    if (written <= 0) {
        return printed;
    }

    size_t len = strnlen(msg, sizeof(msg));
    if (len == 0U || msg[len - 1U] != '\n') {
        return printed;
    }

    while (len > 0U && (msg[len - 1U] == '\n' || msg[len - 1U] == '\r')) {
        msg[--len] = '\0';
    }

    if (len == 0U || s_mirror_in_progress) {
        return printed;
    }

    // Skip ESP-IDF log lines ("I (123) ...", "W (123) ...") to keep Event stream useful.
    if ((msg[0] == 'I' || msg[0] == 'W' || msg[0] == 'E' || msg[0] == 'D' || msg[0] == 'V') && msg[1] == ' ') {
        return printed;
    }

    yamcs_log_start_sender_task_if_needed();
    if (s_log_queue == NULL) {
        return printed;
    }

    YAMCS_LogItem_t item;
    memset(&item, 0, sizeof(item));
    memcpy(item.text, msg, len);

    // Non-blocking enqueue: if queue is full, drop oldest debug line.
    (void)xQueueSend(s_log_queue, &item, 0);

    return printed;
}

int YAMCS_LogPrintf(const char *fmt, ...) {
    if (fmt == NULL) {
        return 0;
    }

    va_list args;
    va_start(args, fmt);
    int rc = yamcs_printf_common(fmt, args);
    va_end(args);
    return rc;
}

int __wrap_printf(const char *fmt, ...) {
    if (fmt == NULL) {
        return 0;
    }

    va_list args;
    va_start(args, fmt);
    int rc = yamcs_printf_common(fmt, args);
    va_end(args);
    return rc;
}
