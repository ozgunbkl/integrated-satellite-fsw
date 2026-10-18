#ifndef TEST_STARVATION_TASK_H
#define TEST_STARVATION_TASK_H
#include "freertos/FreeRTOS.h"
#include <stdatomic.h>
typedef void *TaskHandle_t;
typedef uint8_t StackType_t;
typedef unsigned UBaseType_t;
typedef void (*TaskFunction_t)(void *);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                                  void *argument, UBaseType_t priority, TaskHandle_t *handle, BaseType_t core);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
void TEST_Nop(void);
void TEST_DisableInterrupts(void);
// Escape the real busy loop in the host fixture, after checking IRQ/flush order.
#undef atomic_signal_fence
#define atomic_signal_fence(order) TEST_Nop()
#define portDISABLE_INTERRUPTS() TEST_DisableInterrupts()
#define pdPASS 1
#define configMAX_PRIORITIES 25
BaseType_t xPortGetCoreID(void);
void vTaskDelay(TickType_t ticks);
#endif
