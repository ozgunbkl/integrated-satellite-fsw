#include "recovery_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "nvs.h"
#include <string.h>

static SemaphoreHandle_t s_mutex;
bool RECOVERY_PORT_InitLock(void) { s_mutex = xSemaphoreCreateMutex(); return s_mutex != NULL; }
bool RECOVERY_PORT_Lock(void) { return s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE; }
void RECOVERY_PORT_Unlock(void) { (void)xSemaphoreGive(s_mutex); }
RecoveryLoad_t RECOVERY_PORT_Load(uint8_t record[RECOVERY_RECORD_SIZE]) {
    nvs_handle_t handle;
    esp_err_t error = nvs_open("fsw_recovery", NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return RECOVERY_LOAD_EMPTY;
    if (error != ESP_OK) return RECOVERY_LOAD_ERROR;
    size_t size = RECOVERY_RECORD_SIZE;
    error = nvs_get_blob(handle, "summary", record, &size);
    nvs_close(handle);
    if (error == ESP_ERR_NVS_NOT_FOUND) return RECOVERY_LOAD_EMPTY;
    return error == ESP_OK && size == RECOVERY_RECORD_SIZE ? RECOVERY_LOAD_OK : RECOVERY_LOAD_ERROR;
}
bool RECOVERY_PORT_Save(const uint8_t record[RECOVERY_RECORD_SIZE]) {
    nvs_handle_t handle;
    if (nvs_open("fsw_recovery", NVS_READWRITE, &handle) != ESP_OK) return false;
    bool ok = nvs_set_blob(handle, "summary", record, RECOVERY_RECORD_SIZE) == ESP_OK && nvs_commit(handle) == ESP_OK;
    uint8_t readback[RECOVERY_RECORD_SIZE];
    size_t size = sizeof(readback);
    ok = ok && nvs_get_blob(handle, "summary", readback, &size) == ESP_OK && size == sizeof(readback) &&
        memcmp(record, readback, sizeof(readback)) == 0;
    nvs_close(handle);
    return ok;
}
bool RECOVERY_PORT_IsBadReset(unsigned reason) {
    // Future/unknown reasons fail closed. SDK POWERON/EXT/SW are not proof of
    // failure; the unfinished-boot counter and sticky history remain independent.
    return reason != ESP_RST_POWERON && reason != ESP_RST_EXT && reason != ESP_RST_SW;
}
