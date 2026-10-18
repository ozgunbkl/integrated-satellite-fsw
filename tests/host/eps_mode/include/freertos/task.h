#ifndef TEST_EPS_MODE_TASK_H
#define TEST_EPS_MODE_TASK_H
#include "FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);
TickType_t xTaskGetTickCount(void);
#endif
