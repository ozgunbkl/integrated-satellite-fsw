#ifndef TEST_STARTUP_RESOURCES_QUEUE_H
#define TEST_STARTUP_RESOURCES_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
#endif
