#ifndef TEST_WDT_INJECTION_TASK_H
#define TEST_WDT_INJECTION_TASK_H
#include <stdint.h>
typedef void *TaskHandle_t;
typedef uint8_t StackType_t;
typedef unsigned UBaseType_t;
void vTaskSuspend(TaskHandle_t task);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
#endif
