#ifndef TEST_FDIR_TASK_H
#define TEST_FDIR_TASK_H
#include "freertos/FreeRTOS.h"
// Deliberately wider than ESP32's uint8_t to test watermark-to-byte conversion.
typedef uint32_t StackType_t;
unsigned uxTaskGetStackHighWaterMark(void *task);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *previous_wake, TickType_t increment);
#endif
