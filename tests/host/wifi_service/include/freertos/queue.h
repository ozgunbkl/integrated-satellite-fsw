#ifndef WIFI_MOCK_QUEUE_H
#define WIFI_MOCK_QUEUE_H
#include "wifi_mock.h"
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned depth, unsigned item_size);
void vQueueDelete(QueueHandle_t queue);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait);
#define portMAX_DELAY UINT32_MAX
#endif
