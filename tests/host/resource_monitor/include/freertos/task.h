#ifndef TEST_RESOURCE_TASK_H
#define TEST_RESOURCE_TASK_H
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreate(TaskFunction_t task, const char *name, configSTACK_DEPTH_TYPE stack,
                       void *argument, UBaseType_t priority, TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *wake, TickType_t period);
TaskHandle_t xTaskGetHandle(const char *name);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
#endif
