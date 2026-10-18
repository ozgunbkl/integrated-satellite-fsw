#ifndef TEST_WATCHDOG_FREERTOS_H
#define TEST_WATCHDOG_FREERTOS_H
#include <stdint.h>
#ifndef TEST_WDT_HZ
#define TEST_WDT_HZ 100U
#endif
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * TEST_WDT_HZ) / 1000U))
#define portTICK_PERIOD_MS (1000U / TEST_WDT_HZ)
#endif
