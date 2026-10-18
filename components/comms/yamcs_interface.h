#ifndef YAMCS_INTERFACE_H
#define YAMCS_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>

#define YAMCS_DEST_IP "192.168.1.3"  // Set this to your PC IP on the same Wi-Fi network
#define YAMCS_TM_PORT 10015
#define YAMCS_TC_PORT 10025

#define YAMCS_APID_EVENT 2000

// Call once at startup before log initialization or any sending task.
// Returns false on allocation failure or repeated initialization.
bool YAMCS_Init(void);

// Startup-only, one-way enable after esp_netif_init() returns ESP_OK.
// This means the socket infrastructure exists, not that Wi-Fi/IP is connected.
// Readiness defaults to false; Wi-Fi timeout/disconnection must not clear it.
bool YAMCS_MarkNetworkStackReady(void);
bool YAMCS_IsNetworkStackReady(void);

// Live IP connectivity is separate from one-way TCP/IP readiness. Disconnect
// closes the sender socket under its mutex; reconnect opens it on the next send.
bool YAMCS_SetLinkConnected(bool connected);
bool YAMCS_IsLinkConnected(void);

// Calls from tasks are serialized; return false without socket access if the
// sender/network stack is uninitialized, or return false if sending fails.
// APID must fit 11 bits; payload must be non-null and 1..506 bytes.
bool YAMCS_SendTelemetry(uint16_t apid, void *data, uint16_t len);
bool YAMCS_SendEvent(const char *format, ...);

#endif
