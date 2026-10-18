#ifndef TEST_TM_GEN_QUEUE_H
#define TEST_TM_GEN_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
BaseType_t xQueueSend(QueueHandle_t queue, const void *data, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait);
#endif
