#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "recovery_port.h"
#include "nvs.h"
#include "esp_system.h"
#include "freertos/semphr.h"
static const char *scenario;
static unsigned opens, closes, read_calls, sets, commits;
static bool held, writing;
static uint8_t bytes[RECOVERY_RECORD_SIZE];
static bool is(const char *s) { return strcmp(scenario,s)==0; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return is("allocation") ? NULL : (void *)1; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t lock, TickType_t ticks) {
    assert(lock == (void *)1 && ticks == 10 && !held);
    if (is("lock_busy")) return pdFALSE;
    held=true; return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t lock) { assert(lock==(void*)1 && held); held=false; return pdTRUE; }
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle) {
    assert(strcmp(name,"fsw_recovery")==0 && mode==(writing?NVS_READWRITE:NVS_READONLY)); ++opens;
    if (is("namespace_missing")) return ESP_ERR_NVS_NOT_FOUND;
    if (is("open_error")) return ESP_FAIL;
    *handle=7; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle==7); ++closes; }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size) {
    assert(handle==7 && strcmp(key,"summary")==0 && *size==RECOVERY_RECORD_SIZE); ++read_calls;
    if (is("key_missing")) return ESP_ERR_NVS_NOT_FOUND;
    if (is("get_error")) return ESP_FAIL;
    memcpy(value,bytes,sizeof(bytes));
    if (is("wrong_size")) --*size;
    if (is("readback_mismatch")) ((uint8_t*)value)[3]^=1;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size) {
    assert(handle==7 && strcmp(key,"summary")==0 && size==RECOVERY_RECORD_SIZE); ++sets;
    memcpy(bytes,value,size); return is("set_error")?ESP_FAIL:ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) { assert(handle==7); ++commits; return is("commit_error")?ESP_FAIL:ESP_OK; }
int main(int argc, char **argv) {
    assert(argc==2); scenario=argv[1];
    assert(RECOVERY_PORT_InitLock()==!is("allocation"));
    if (!is("allocation")) {
        assert(RECOVERY_PORT_Lock()==!is("lock_busy"));
        if (!is("lock_busy")) RECOVERY_PORT_Unlock();
    }
    for (unsigned reason=0;reason<=16;++reason)
        assert(RECOVERY_PORT_IsBadReset(reason)==(reason!=ESP_RST_POWERON&&reason!=ESP_RST_EXT&&reason!=ESP_RST_SW));
    uint8_t record[RECOVERY_RECORD_SIZE]={42};
    if (strncmp(scenario,"save",4)==0 || is("set_error") || is("commit_error") || is("readback_mismatch")) {
        writing=true;
        const bool ok=RECOVERY_PORT_Save(record);
        assert(ok==is("save_ok"));
        assert(opens==1 && closes==1 && sets==1);
        assert(commits==(is("set_error")?0U:1U));
        assert(read_calls==(is("set_error")||is("commit_error")?0U:1U));
    } else {
        const RecoveryLoad_t result=RECOVERY_PORT_Load(record);
        assert(result==((is("namespace_missing")||is("key_missing"))?RECOVERY_LOAD_EMPTY:
            (is("open_error")||is("get_error")||is("wrong_size"))?RECOVERY_LOAD_ERROR:RECOVERY_LOAD_OK));
        assert(sets==0 && commits==0 && closes==((is("namespace_missing")||is("open_error"))?0U:1U));
    }
    puts("PASS: isolated NVS port case, correct namespace/close/commit/readback; no erase or hardware.");
    return 0;
}
