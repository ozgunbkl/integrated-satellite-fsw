#ifndef TEST_RESOURCE_FREERTOS_H
#define TEST_RESOURCE_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef uint32_t UBaseType_t, TickType_t, configSTACK_DEPTH_TYPE;
typedef uint8_t StackType_t;
#define pdPASS 1
#define pdTRUE 1
#define portTICK_PERIOD_MS 10U
#define pdMS_TO_TICKS(ms) ((TickType_t)((ms) / portTICK_PERIOD_MS))
#endif
