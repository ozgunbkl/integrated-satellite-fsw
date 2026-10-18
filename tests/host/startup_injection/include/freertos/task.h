#ifndef TEST_STARTUP_INJECTION_TASK_H
#define TEST_STARTUP_INJECTION_TASK_H
#include "FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t function, const char *name,
    configSTACK_DEPTH_TYPE stack, void *argument, UBaseType_t priority,
    TaskHandle_t *handle, BaseType_t core);
#endif
