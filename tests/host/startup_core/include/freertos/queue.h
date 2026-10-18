#ifndef TEST_STARTUP_CORE_QUEUE_H
#define TEST_STARTUP_CORE_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t depth, UBaseType_t item_size);
#endif
