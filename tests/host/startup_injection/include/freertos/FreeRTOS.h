#ifndef TEST_STARTUP_INJECTION_FREERTOS_H
#define TEST_STARTUP_INJECTION_FREERTOS_H
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t configSTACK_DEPTH_TYPE;
#define pdPASS 1
#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY (-1)
#define tskNO_AFFINITY 0x7FFFFFFF
#endif
