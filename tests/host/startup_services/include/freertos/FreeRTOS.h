/* RTOS declarations used only by the startup service host check. */
#ifndef TEST_STARTUP_FREERTOS_H
#define TEST_STARTUP_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define portTICK_PERIOD_MS 1U
#endif
