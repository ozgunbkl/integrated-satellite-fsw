#ifndef TEST_RECOVERY_SEMPHR_H
#define TEST_RECOVERY_SEMPHR_H
#include "FreeRTOS.h"
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t handle);
#endif
