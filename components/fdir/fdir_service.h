#ifndef FDIR_SERVICE_H
#define FDIR_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "system_fault.h"



// Defined Fault Models

typedef enum{
    FAULT_INFO,
    FAULT_WARNING,
    FAULT_CRITICAL,
    FAULT_FATAL
} FaultSeverity_t;

typedef enum {
    SRC_PAYLOAD,
    SRC_EPS,
    SRC_COMMS,
    SRC_ARCHIVE,
    SRC_CDH,
    SRC_ADCS,
    SRC_COUNT
} FaultSource_t;

typedef struct {
    FaultSource_t source;
    FaultSeverity_t severity;
    uint16_t fault_code;
    uint32_t timestamp;
} FaultReport_t;


// FDIR Public API
bool FDIR_Init(void);
void FDIR_ReportFault(FaultReport_t report);
// Task context; consumes thresholds only after a guarded DEGRADED decision.
// Failure/BOOT/safety modes retain warnings for a later call or ground ACK.
void FDIR_Process(void);
// Warning counters OR active critical/fatal ledger/uncertainty; failure -> true.
bool FDIR_HasActiveFaults(void);
typedef SystemFaultSnapshot_t FDIR_ActiveFaultSnapshot_t;
bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out);
// Quiet MODE-ledger APIs, no warning mutex/I/O. May be used under EPS data lock
// (EPS -> mode). Only the trusted fault owner can resolve after validated health.
bool FDIR_GetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision);
bool FDIR_ResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision);
// Zero-wait owner variants: busy ledger -> false, no clear/output mutation.
bool FDIR_TryGetFaultRevision(FaultSource_t source, uint16_t code, uint32_t *revision);
bool FDIR_TryResolveCriticalFault(FaultSource_t source, uint16_t code, uint32_t revision);

void FDIR_ProcessCommand(const uint8_t* payload, uint16_t len);

#endif
