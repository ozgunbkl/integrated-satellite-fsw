#ifndef TEST_FREERTOS_QUEUE_H
#define TEST_FREERTOS_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t wait);
#endif
