#ifndef TEST_WATCHDOG_TASK_H
#define TEST_WATCHDOG_TASK_H
#include "FreeRTOS.h"
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
#endif
