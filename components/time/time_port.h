#ifndef TIME_PORT_H
#define TIME_PORT_H

#include <stdint.h>

// Internal platform contract used by the platform-independent time service.
void TIME_PortInit(void);
uint64_t TIME_PortGetMicroseconds(void);

#endif // TIME_PORT_H
