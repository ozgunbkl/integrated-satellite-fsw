#ifndef TEST_TM_GEN_FREERTOS_H
#define TEST_TM_GEN_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFAIL 0
// Deliberately not a 1 ms tick: packet ticks and report milliseconds differ.
#define portTICK_PERIOD_MS 10U
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))
#endif
