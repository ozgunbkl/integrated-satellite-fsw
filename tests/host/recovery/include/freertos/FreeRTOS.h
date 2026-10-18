#ifndef TEST_RECOVERY_FREERTOS_H
#define TEST_RECOVERY_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t UBaseType_t, TickType_t, configSTACK_DEPTH_TYPE;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif
