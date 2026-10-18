#ifndef EPS_COMMANDS_H
#define EPS_COMMANDS_H

#include <stdint.h>

void EPS_ProcessCommand(const uint8_t* payload, uint16_t len);
void EPS_Monitor(float battery_voltage);

// --- EPS FAULT REGISTRY ---
#define FAULT_EPS_HEATER_FAIL    0xE101  // Heater didn't toggle
#define FAULT_EPS_LOW_VOLTAGE    0xE102  // Unusable bus voltage: below threshold or non-finite.
#define FAULT_EPS_UNKNOWN_CMD    0xE103  // Ground sent invalid EPS command

#endif
