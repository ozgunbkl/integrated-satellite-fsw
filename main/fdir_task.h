#ifndef FSW_FDIR_TASK_H
#define FSW_FDIR_TASK_H

// ESP-IDF xTaskCreate uses bytes. Covers warning processing + wrapped printf;
// 2048 bytes overflowed on the real archive-full warning path.
#define FDIR_TASK_STACK_BYTES 4096U

// Application adapter: periodic FDIR processing plus watchdog heartbeat.
void vFDIRTask(void *argument);

#endif
