#ifndef TEST_YAMCS_SEMPHR_H
#define TEST_YAMCS_SEMPHR_H
#include "FreeRTOS.h"
typedef struct HostSemaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
#endif
