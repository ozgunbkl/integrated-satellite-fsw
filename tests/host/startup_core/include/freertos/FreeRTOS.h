#ifndef TEST_STARTUP_CORE_FREERTOS_H
#define TEST_STARTUP_CORE_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t UBaseType_t, TickType_t, configSTACK_DEPTH_TYPE;
#define pdPASS 1
#define pdTRUE 1
#define pdFALSE 0
#define configMAX_PRIORITIES 25
#endif
