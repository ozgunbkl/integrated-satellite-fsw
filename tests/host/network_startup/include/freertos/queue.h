#ifndef TEST_NETWORK_QUEUE_H
#define TEST_NETWORK_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *out, TickType_t wait);
#endif
