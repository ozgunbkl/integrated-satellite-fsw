#ifndef TEST_STARTUP_CORE_TASK_H
#define TEST_STARTUP_CORE_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t function, const char *name, configSTACK_DEPTH_TYPE stack,
                      void *argument, UBaseType_t priority, TaskHandle_t *handle);
#endif
