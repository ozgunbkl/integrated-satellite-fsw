#ifndef STARTUP_CORE_H
#define STARTUP_CORE_H

// Startup only, once per boot before required resources, networking and tasks.
// Returns only after all common services have been initialized in order.
// Existing critical service failures remain fatal (abort/panic policy).
// Repeated calls are fatal before any service is touched: no clock/archive reset,
// no implicit reinitialization, retry or rollback of partially initialized state.
void STARTUP_InitCoreServices(void);

#endif
