#ifndef TEST_RESOURCE_QUEUE_H
#define TEST_RESOURCE_QUEUE_H
#include "FreeRTOS.h"
typedef void *QueueHandle_t;
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
#endif
