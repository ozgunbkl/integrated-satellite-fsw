#ifndef FSW_MISSION_IDS_H
#define FSW_MISSION_IDS_H

// Existing mission routing IDs; one owner for router, packet and command users.
#define APID_ADCS    0x010
#define APID_EPS     0x020
#define APID_FDIR    0x030
#define APID_CDHS    0x040
#define APID_HK      0x050
#define APID_ARCHIVE 0x060
#define APID_ARCHIVE_TRANSFER 0x061 // TM replies only; transfer TC still uses 0x060.
#define APID_PAYLOAD 0x070
#define APID_IDLE    0x7FF  // CCSDS idle/fill packet

#endif // FSW_MISSION_IDS_H
