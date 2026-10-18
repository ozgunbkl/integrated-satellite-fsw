/* Frozen mission contracts, independent of RTOS, SDK and CDH services. */
#include "satellite_types.h"
#include "commands.h"
#include "ccsds_packet.h"
#include <stddef.h>
#include <stdio.h>

_Static_assert(CCSDS_HEADER_SIZE == 14U && CCSDS_CRC_SIZE == 2U &&
               TELECOMMAND_MIN_SIZE == 17U && TELECOMMAND_MAX_SIZE == 64U,
               "Internal TC frame bounds changed");
_Static_assert(sizeof(CCSDS_PrimaryHeader_t) == 6 && sizeof(CCSDS_SecondaryHeader_t) == 8 &&
               CCSDS_HEADER_SIZE == sizeof(CCSDS_PrimaryHeader_t) + sizeof(CCSDS_SecondaryHeader_t),
               "CCSDS header layout changed");
_Static_assert(sizeof(TelecommandPacket_t) == 68 &&
               offsetof(TelecommandPacket_t, raw_data) == 0 &&
               offsetof(TelecommandPacket_t, length) == 64 &&
               offsetof(TelecommandPacket_t, packet_crc) == 66,
               "Internal command queue item layout changed");
_Static_assert(sizeof(HK_Telemetry_t) == 20 &&
               offsetof(HK_Telemetry_t, timestamp) == 0 &&
               offsetof(HK_Telemetry_t, sequence_count) == 4 &&
               offsetof(HK_Telemetry_t, status_flags) == 6 &&
               offsetof(HK_Telemetry_t, bus_voltage) == 10 &&
               offsetof(HK_Telemetry_t, ext_temp_c) == 14 &&
               offsetof(HK_Telemetry_t, crc_checksum) == 18,
               "Existing GCC HK queue/archive layout changed");

_Static_assert(APID_ADCS == 0x010 && APID_EPS == 0x020 && APID_FDIR == 0x030 &&
               APID_CDHS == 0x040 && APID_HK == 0x050 && APID_ARCHIVE == 0x060 &&
               APID_ARCHIVE_TRANSFER == 0x061 && APID_PAYLOAD == 0x070 && APID_IDLE == 0x7FF, "Mission APIDs changed");
_Static_assert(ADCS_CMD_SET_MODE == 0x01 && ADCS_CMD_RESET_ESTIMATION == 0x05,
               "ADCS command IDs changed");
_Static_assert(EPS_CMD_HEATER_CTRL == 0x10 && EPS_CMD_BUS_RESET == 0x20,
               "EPS command IDs changed");
_Static_assert(HK_CMD_SET_INTERVAL == 0x01 && HK_CMD_FORCE_REPORT == 0x02,
               "HK command IDs changed");
_Static_assert(ARCHIVE_CMD_GET_BY_INDEX == 0x30 && ARCHIVE_CMD_WIPE_ALL == 0x3F,
               "Archive command IDs changed");
_Static_assert(ARCHIVE_CMD_BEGIN_TRANSFER == 0x31 && ARCHIVE_CMD_PENDING_TRANSFER == 0x32 &&
               ARCHIVE_CMD_READ_TRANSFER == 0x33 && ARCHIVE_CMD_CONFIRM_TRANSFER == 0x34 &&
               ARCHIVE_CMD_CANCEL_TRANSFER == 0x35 && ARCHIVE_CONFIRM_KEY == 0xA5,
               "Transfer wire command IDs changed");
_Static_assert(FDIR_CMD_ACK_FAULT == 0x50 && FDIR_CMD_RESET_MODE == 0x52,
               "FDIR command IDs changed");
_Static_assert(PAY_CMD_INIT == 0 && PAY_CMD_START == 1 && PAY_CMD_STOP == 2 &&
               PAY_CMD_SET_RATE == 3 && PAY_CMD_RESET == 4, "Payload command IDs changed");
_Static_assert(CDH_CMD_REBOOT == 0xFF && CDH_CMD_SYNC_TIME == 0x70,
               "CDH wire command IDs changed");
_Static_assert(FAULT_CDH_UNKNOWN_APID == 0xC101 && FAULT_CDH_PACKET_TOO_SHORT == 0xC102 &&
               FAULT_CDH_ROUTING_ERROR == 0xC103, "Router fault IDs changed");
_Static_assert(TC_NO_OP == 0 && TC_SET_MODE == 1 && TC_REQUEST_HK == 2 &&
               CDH_ENUM_REBOOT == 3 && CDH_ENUM_SYNC_TIME == 4, "Legacy internal TC IDs changed");
_Static_assert(WDT_TASK_TM_GEN == 0 && WDT_TASK_CMD_PROC == 1 && WDT_TASK_EPS_MON == 2 &&
               WDT_TASK_DATA_LOG == 3 && WDT_TASK_ADCS == 4 && WDT_TASK_EPS_MANAGER == 5 &&
               WDT_TASK_FDIR == 6 && WDT_TASK_COUNT == 7, "Watchdog role IDs changed");
_Static_assert(DOWNLINK_INACTIVE == 0 && DOWNLINK_ACTIVE == 1, "Downlink state IDs changed");

int main(void) {
    puts("PASS: mission APID/command/fault IDs, TC/HK layouts and watchdog roles preserved.");
    return 0;
}
