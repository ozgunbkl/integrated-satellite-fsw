#ifndef COMMS_FRAME_H
#define COMMS_FRAME_H

#include <stdint.h>
#include <stddef.h>

// Frame Constants
#define FRAME_START_BYTE 0xAA   // Synchronization byte (10101010 in binary)
#define MAX_PAYLOAD_SIZE 64     // Maximum data size for one packet

// Command IDs
#define CMD_ORBIT_MAINTENANCE 0xA1
#define CMD_THERMAL_CONTROL  0xB2

// Satellite States (Variables that the commands will change)
extern uint8_t g_thruster_duration; // How long to fire (seconds)
extern int8_t g_target_temperature; // Target heat (Celsius)
extern uint8_t g_heater_status;       // 0 = OFF, 1 = ON
extern uint32_t g_satellite_altitude; // Altitude in meters

/**
 * @brief The Communication Frame Structure
 */
typedef struct {
    uint8_t start_byte;             // Sync byte
    uint8_t length;                 // How many bytes are in the payload
    uint8_t payload[MAX_PAYLOAD_SIZE];    // Instruction or data
    uint16_t crc;                   // Checksum for the whole packet
} comms_frame_t;

// Function Prototypes
uint16_t COMMS_CalculateCRC16(const uint8_t *data, size_t length);

/**
 * @brief Packages raw data into a structured frame.
 * @param frame Pointer to the frame structure to be filled.
 * @param payload Raw data to send.
 * @param length Size of the raw data.
 */
void COMMS_CreateFrame(comms_frame_t *frame, const uint8_t *payload, uint8_t length);

/**
 * @brief Processes a single byte received from the radio.
 */
int COMMS_ParseByte(uint8_t byte);

void COMMS_DispatchCommand(comms_frame_t *frame);

void COMMS_GenerateTelemetry(comms_frame_t *out_frame);

void COMMS_ResetParser(void);

typedef enum {
    STATE_SEARCHING_FOR_START,
    STATE_READING_LENGTH,
    STATE_READING_PAYLOAD,
    STATE_VERIFYING_CRC
} parser_state_t;

#endif
