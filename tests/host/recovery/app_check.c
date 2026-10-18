#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "recovery_app.h"
#include "recovery.h"
#include "startup_nvs.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "eps_telemetry.h"
#include "fdir_service.h"
#include "watchdog.h"
#include "archive_service.h"
#include "payload_service.h"
#include "system_mode.h"
#include "time_service.h"
static const char *scenario;
static TaskFunction_t worker;
static jmp_buf done;
static unsigned init_calls, nvs_calls, disabled, polls, reads, creates;
static bool eps_locked;
static RecoveryObservation_t observed;
EPS_Telemetry_t g_eps_telemetry = {.f_BusVoltage=8.5f};
SemaphoreHandle_t xEPSDataMutex=(void*)1;
static bool is(const char *s) { return strcmp(scenario,s)==0; }
esp_err_t STARTUP_InitNvs(void) { ++nvs_calls; return is("nvs_failure")?ESP_FAIL:ESP_OK; }
esp_reset_reason_t esp_reset_reason(void) { return ESP_RST_PANIC; }
bool RECOVERY_Init(bool available, unsigned reason) {
    assert(available==!is("nvs_failure")&&reason==ESP_RST_PANIC&&nvs_calls==1); ++init_calls;
    return !is("nvs_failure");
}
void RECOVERY_Disable(void) { ++disabled; }
bool RECOVERY_GetSnapshot(RecoverySnapshot_t *out) {
    assert(!eps_locked);
    *out=(RecoverySnapshot_t){.boot_count=7,.held=true,.ready=observed.healthy,.storage_ok=true}; return true;
}
RecoveryResult_t RECOVERY_Poll(const RecoveryObservation_t *o,uint32_t now) {
    assert(!eps_locked&&now==123000); observed=*o; ++polls;
    return is("rearmed")?RECOVERY_REARMED:is("storage_failed")?RECOVERY_STORAGE_FAILED:RECOVERY_NO_RESULT;
}
uint64_t TIME_GetMilliseconds(void) { return 123000; }
BaseType_t xTaskCreate(TaskFunction_t function,const char *name,configSTACK_DEPTH_TYPE stack,
                      void *argument,UBaseType_t priority,TaskHandle_t *handle) {
    assert(!strcmp(name,"RECOVERY")&&stack==6144&&priority==1&&argument==NULL&&handle==NULL);
    ++creates; worker=function; return is("create_failure")?pdFALSE:pdPASS;
}
void vTaskDelay(TickType_t ticks) { assert(ticks==1000&&!eps_locked); longjmp(done,1); }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { assert(task==NULL&&!eps_locked); return 2048; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock,TickType_t ticks) {
    assert(lock==xEPSDataMutex&&ticks==10&&!eps_locked);
    if (is("eps_lock")) return pdFALSE;
    eps_locked=true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock==xEPSDataMutex&&eps_locked); eps_locked=false; return is("eps_give_failure")?pdFALSE:pdTRUE; }
ArchiveStatus_t Archive_ReadRecord(uint16_t index,ArchiveRecord_t *out) {
    assert(!eps_locked&&index<30); ++reads;
    if (is("archive_crc")) return ARCHIVE_ERR_CRC;
    if (is("archive_busy")) return ARCHIVE_ERR_UNAVAILABLE;
    if (index==2) return ARCHIVE_ERR_NOT_FOUND;
    memset(out,0,sizeof(*out)); return ARCHIVE_OK;
}
bool FDIR_GetActiveFaults(FDIR_ActiveFaultSnapshot_t *out) {
    assert(!eps_locked);
    if (is("snapshot_failure")) return false;
    memset(out,0,sizeof(*out));
    out->recording_uncertain=is("uncertain"); out->overflow=is("overflow");
    if (is("critical")||is("fatal")) {
        out->count=1;
        out->records[0]=(SystemFaultRecord_t){.source=3,.code=0xAF02,.last_timestamp=42,.fatal=is("fatal")};
    }
    return true;
}
bool SYSTEM_MODE_IsDeploymentComplete(void) { assert(!eps_locked); return !is("undeployed"); }
SystemMode_t SYSTEM_MODE_Get(void) { assert(!eps_locked); return is("emergency")?MODE_EMERGENCY:is("critical_mode")?MODE_CRITICAL:MODE_SAFE; }
PayloadTelemetry_t Payload_GetTelemetry(void) {
    assert(!eps_locked); return (PayloadTelemetry_t){.current_state=is("payload_error")?PL_STATE_ERROR:PL_STATE_SAFE};
}
bool WATCHDOG_GetTimingSnapshot(WATCHDOG_TaskTiming_t out[WDT_TASK_COUNT]) {
    assert(!eps_locked);
    if (is("timing_failure")) return false;
    for (unsigned i=0;i<WDT_TASK_COUNT;++i) out[i]=(WATCHDOG_TaskTiming_t){.pet_count=20,.age_ms=100};
    if (is("no_pet")) out[0].pet_count=0;
    if (!strncmp(scenario,"stale_",6)) out[(unsigned)(scenario[6]-'0')].age_ms=WATCHDOG_TIMEOUT_MS+1;
    return true;
}
int __wrap_printf(const char *format,...) {
    assert(!eps_locked); va_list args; va_start(args,format); char text[512];
    int n=vsnprintf(text,sizeof(text),format,args); va_end(args); assert(n>=0&&(size_t)n<sizeof(text)); return n;
}
int main(int argc,char **argv) {
    assert(argc==2); scenario=argv[1];
    if (is("voltage_low")) g_eps_telemetry.f_BusVoltage=4.9f;
    if (is("voltage_nan")) g_eps_telemetry.f_BusVoltage=NAN;
    if (is("voltage_inf")) g_eps_telemetry.f_BusVoltage=INFINITY;
    RECOVERY_APP_Prepare(); assert(init_calls==1);
    assert(RECOVERY_APP_Start()==!is("create_failure"));
    if (is("create_failure")) { assert(disabled==1); return 0; }
    if (is("repeat_start")) { assert(!RECOVERY_APP_Start()&&disabled==1&&creates==1); return 0; }
    if (setjmp(done)==0) worker(NULL);
    assert(polls==1&&reads<=3&&!eps_locked);
    const bool healthy=is("healthy")||is("rearmed")||is("storage_failed")||is("nvs_failure");
    assert(observed.healthy==healthy);
    assert(observed.active_fault==(is("critical")||is("fatal")));
    assert(observed.uncertain==is("uncertain")&&observed.fatal==is("fatal")&&observed.overflow==is("overflow"));
    puts("PASS: recovery app observer/startup case; real adapter, service/RTOS mocks, no hardware.");
    return 0;
}
