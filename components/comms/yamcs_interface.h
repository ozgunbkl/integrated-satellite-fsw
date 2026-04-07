#ifndef YAMCS_INTERFACE_H
#define YAMCS_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

#define YAMCS_DEST_IP "192.168.1.3"  // Set this to your PC IP on the same Wi-Fi network
#define YAMCS_TM_PORT 10015
#define YAMCS_TC_PORT 10025

#define YAMCS_APID_EVENT 2000

bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t len);
bool YAMCS_SendEvent(const char *format, ...);

#endif
