#ifndef EPS_CONTROL_H
#define EPS_CONTROL_H

// EPS monitoring task entry point; the RTOS dependency belongs to its implementation.
void vEPSMonitoringTask(void *pvParameters);

// Legacy diagnostic stub; does not apply a power policy or change the system mode.
void vEPS_SetSafeModePower(int mode_id); 

#endif // EPS_CONTROL_H
