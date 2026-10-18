/* Minimal declarations for the real ADCS task's one-cycle host check. */
#ifndef TEST_ADCS_STARTUP_FREERTOS_H
#define TEST_ADCS_STARTUP_FREERTOS_H
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#endif
