#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include <stdint.h>

// Initializes the Time Service. MET starts at 0.
void TIME_Init(void);

// Returns the Mission Elapsed Time (MET) in whole seconds.
uint32_t TIME_GetSeconds(void);

// Returns the Mission Elapsed Time (MET) in milliseconds.
// Useful for high-resolution telemetry and control loops.
uint64_t TIME_GetMilliseconds(void);

#endif // TIME_SERVICE_H
