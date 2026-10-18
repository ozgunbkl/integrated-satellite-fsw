#ifndef RECOVERY_PORT_H
#define RECOVERY_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "recovery.h"
typedef enum { RECOVERY_LOAD_OK, RECOVERY_LOAD_EMPTY, RECOVERY_LOAD_ERROR } RecoveryLoad_t;
bool RECOVERY_PORT_InitLock(void);
bool RECOVERY_PORT_Lock(void);
void RECOVERY_PORT_Unlock(void);
RecoveryLoad_t RECOVERY_PORT_Load(uint8_t record[RECOVERY_RECORD_SIZE]);
bool RECOVERY_PORT_Save(const uint8_t record[RECOVERY_RECORD_SIZE]);
bool RECOVERY_PORT_IsBadReset(unsigned reason);
#endif
