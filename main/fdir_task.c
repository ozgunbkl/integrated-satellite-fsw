#include "fdir_task.h"
#include "fdir_service.h"
#include "watchdog.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

void vFDIRTask(void *argument) {
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    unsigned stack_report_cycles = 0;
    printf("FDIR: Warning processing task initialized (1000 ms period).\n");

    for (;;) {
        FDIR_Process();
        watchdog_pet(WDT_TASK_FDIR);
        // The watermark includes earlier warning/logging peaks. Periodic rather
        // than per-cycle output also gives fresh evidence after a warning batch.
        if (++stack_report_cycles == 30U) {
            stack_report_cycles = 0;
            printf("FDIR: stack_free_min_bytes=%u.\n",
                   (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}
