#ifndef RECOVERY_APP_H
#define RECOVERY_APP_H
#include <stdbool.h>
// Prepare runs first, before core init, and commits the unfinished-boot marker.
// Failed storage preserves a conservative operating gate, without NVS erase.
void RECOVERY_APP_Prepare(void);
// Separate low-priority observer; false leaves the permanent per-boot gate set.
bool RECOVERY_APP_Start(void);
#endif
