#ifndef COMMANDS_H
#define COMMANDS_H

// --- ADCS COMMAND DICTIONARY (APID 0x010) ---

// Rule: Command ID 0x01 = Set Mode
// Payload: [ID] [MODE_BYTE]
#define ADCS_CMD_SET_MODE          0x01

// Rule: Command ID 0x05 = Reset Estimation
// Payload: [ID]
#define ADCS_CMD_RESET_ESTIMATION   0x05

// --- EPS COMMAND DICTIONARY (APID 0x020) ---

// Rule: Command ID 0x10 = Set Heater State
// Payload: [0x10] [0x01 for ON, 0x00 for OFF]
#define EPS_CMD_HEATER_CTRL        0x10

// Rule: Command ID 0x20 = Reset Power Bus
// Payload: [0x20]
#define EPS_CMD_BUS_RESET          0x20


// --- HK COMMAND DICTIONARY (APID_HK = 0x050) ---

// Rule: Command ID 0x01 = Set Telemetry Interval
// Payload: [0x01] [Interval in Seconds]
#define HK_CMD_SET_INTERVAL        0x01

// Rule: Command ID 0x02 = Force Immediate Report
// Payload: [0x02]
#define HK_CMD_FORCE_REPORT        0x02

// --- ARCHIVE COMMAND DICTIONARY (APID_ARCHIVE = 0x060) ---

// Rule: Command ID 0x30 = Request Data by Index
// Payload: [0x30] [Index Number]
#define ARCHIVE_CMD_GET_BY_INDEX   0x30

// Transfer commands (all on TC APID 0x060). Manifest integers are big-endian:
// boot:u64, transfer:u32, request:u32, digest:u32, count:u16 (22 bytes).
#define ARCHIVE_CMD_BEGIN_TRANSFER    0x31 // ID + request:u32
#define ARCHIVE_CMD_PENDING_TRANSFER  0x32 // ID only (read-only discovery)
#define ARCHIVE_CMD_READ_TRANSFER     0x33 // ID + manifest + index:u16
#define ARCHIVE_CMD_CONFIRM_TRANSFER  0x34 // ID + manifest + explicit 0xA5 key
#define ARCHIVE_CMD_CANCEL_TRANSFER   0x35 // ID + manifest (no deletion)
#define ARCHIVE_CONFIRM_KEY           0xA5

// Rule: Command ID 0x3F = Clear All Stored Logs (Dangerous!)
// Payload: [0x3F] [0xAA] (0xAA is a safety "key" to prevent accidental wipes)
#define ARCHIVE_CMD_WIPE_ALL       0x3F

// --- FDIR COMMAND DICTIONARY (APID_FDIR = 0x030) ---

// Rule: Command ID 0x50 = Acknowledge Fault
// Payload: [0x50]; clears warning counters only, not critical/fatal records.
#define FDIR_CMD_ACK_FAULT         0x50  

// Rule: Command ID 0x52 = Request System Mode (subject to mode/fault guards)
// Payload: [0x52] [SystemMode_t byte]
#define FDIR_CMD_RESET_MODE        0x52
// Separate per-boot recovery approval: [0x53][boot counter:u32 BE][0xA5].
// Queue acceptance is not completion; never clears faults or selects NOMINAL.
#define FDIR_CMD_REARM_BOOT        0x53

// --- PAYLOAD COMMAND DICTIONARY (APID_PAYLOAD = 0x070) ---
#define PAY_CMD_INIT       0x00
#define PAY_CMD_START      0x01
#define PAY_CMD_STOP       0x02
#define PAY_CMD_SET_RATE   0x03
#define PAY_CMD_RESET      0x04

// --- CDHS INTERNAL DICTIONARY (APID_CDHS = 0x040) ---
#define CDH_CMD_REBOOT      0xFF
#define CDH_CMD_SYNC_TIME   0x70

// --- CDHS ROUTER FAULT REGISTRY ---
#define FAULT_CDH_UNKNOWN_APID    0xC101  // Ground sent a packet for a non-existent subsystem
#define FAULT_CDH_PACKET_TOO_SHORT 0xC102  // Packet is below the internal TC frame minimum
#define FAULT_CDH_ROUTING_ERROR   0xC103  // Failed to pass data to a subsystem

#endif
