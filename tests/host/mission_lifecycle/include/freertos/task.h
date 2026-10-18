#ifndef TEST_MISSION_TASK_H
#define TEST_MISSION_TASK_H
#include "freertos/FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);
void vTaskSuspend(void *task);
TickType_t xTaskGetTickCount(void);
unsigned uxTaskGetStackHighWaterMark(void *task);
#endif
