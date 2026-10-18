#ifndef EPS_SERVICE_H
#define EPS_SERVICE_H

#include "system_mode.h"

typedef enum {
    EPS_OP_DETUMBLE,
    EPS_OP_POINTING,
    EPS_OP_ORBIT_BOOST,
    EPS_OP_PAYLOAD
} EpsOperation_t;

// Task-context authorization against live EPS state/PDU and global mode.
// Order: caller service mutex -> EPS data -> system mode. The short action
// must not block, log, call FDIR/EPS/system-mode APIs, or acquire other locks.
// False means action was not called (denied, uninitialized, or lock timeout).
bool EPS_RunIfPermitted(EpsOperation_t operation, SystemModeAction_t action, void *context);

#endif
