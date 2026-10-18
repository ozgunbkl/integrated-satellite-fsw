// Actual optional Wi-Fi service. SDK/RTOS calls are deterministic mocks.
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "include/wifi_mock.h"
#include "wifi_service.h"
#include "yamcs_interface.h"
#include "yamcs_log.h"
#include "startup_nvs.h"
#include "startup_network.h"

const char WIFI_EVENT[] = "wifi", IP_EVENT[] = "ip";
const esp_netif_inherent_config_t host_netif_config = {.if_key = "WIFI_STA_DEF"};
const char *host_ssid = "host-test";
struct HostEvent { uint32_t bits; bool freed; };
static struct HostEvent group;
static esp_netif_t station;
static const char *scenario;
static unsigned calls, destroys, clears, deinit_calls, stops, unregisters, tasks;
static unsigned nvs_calls, stack_calls, event_calls, connects, disconnects, log_calls;
static bool link_connected, real_runtime, config_started;
static uint32_t clock_ms;
static char last_error[256];
static TaskFunction_t manager;
static esp_event_handler_t handler;
static jmp_buf manager_exit;

static bool is(const char *name) { return strcmp(scenario, name) == 0; }
static esp_err_t fail_at(const char *name) { calls++; return is(name) ? ESP_ERR_NO_MEM : ESP_OK; }
const char *esp_err_to_name(esp_err_t code) { return code == ESP_OK ? "ESP_OK" : "MOCK_ERROR"; }
void host_log(const char *tag, const char *format, ...) {
    assert(strcmp(tag, "MAIN") == 0);
    va_list args; va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
}
esp_err_t STARTUP_InitNvs(void) { nvs_calls++; return fail_at("nvs"); }
esp_err_t STARTUP_InitNetworkStack(void) { stack_calls++; return fail_at("stack"); }
esp_err_t STARTUP_CreateWifiEventLoop(void) { event_calls++; return fail_at("event"); }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    assert(strcmp(key, "WIFI_STA_DEF") == 0); return is("existing") ? &station : NULL;
}
esp_netif_t *esp_netif_new(const esp_netif_config_t *config) {
    assert(config->base == &host_netif_config); calls++; return is("allocation") ? NULL : &station;
}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif) { assert(netif == &station); return fail_at("attach"); }
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void) { return fail_at("defaults"); }
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *netif) {
    assert(netif == &station); clears++;
    return is("clear-cleanup") ? ESP_FAIL : ESP_OK;
}
void esp_netif_destroy(esp_netif_t *netif) { assert(netif == &station); destroys++; }
EventGroupHandle_t xEventGroupCreate(void) { return is("group") ? NULL : &group; }
void vEventGroupDelete(EventGroupHandle_t handle) { assert(handle == &group); group.freed = true; }
EventBits_t xEventGroupGetBits(EventGroupHandle_t handle) { assert(handle == &group && !group.freed); return group.bits; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t handle, EventBits_t bits) { assert(handle == &group && !group.freed); return group.bits |= bits; }
EventBits_t xEventGroupClearBits(EventGroupHandle_t handle, EventBits_t bits) {
    assert(handle == &group && !group.freed); const uint32_t before = group.bits; group.bits &= ~bits; return before;
}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t handle, EventBits_t bits, BaseType_t clear, BaseType_t all, TickType_t wait) {
    assert(handle == &group && !group.freed);
    if (wait != 0) {
        assert(wait == 15000 && all && !clear);
        clock_ms += wait; // No fake IP: exercises actual timeout and late connect.
        if (is("connected")) {
            ip_event_got_ip_t event = {.ip_info.ip.addr = 5};
            handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
            group.bits |= BIT4; // The scheduled manager attempted log init (it may fail).
        }
    }
    const uint32_t before = group.bits;
    if (clear) { group.bits &= ~bits; }
    return before;
}
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { assert(config->magic == 42); return fail_at("init"); }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage == WIFI_STORAGE_RAM); return fail_at("storage"); }
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id, esp_event_handler_t fn,
                                              void *arg, esp_event_handler_instance_t *instance) {
    assert(id == ESP_EVENT_ANY_ID && arg == NULL);
    const esp_err_t result = fail_at(base == WIFI_EVENT ? "wifi-handler" : "ip-handler");
    if (result == ESP_OK) { handler = fn; *instance = base == WIFI_EVENT ? (void *)1 : (void *)2; }
    return result;
}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance) {
    assert(id == ESP_EVENT_ANY_ID && instance != NULL && !group.freed); unregisters++;
    return is(base == IP_EVENT ? "ip-unregister" : "wifi-unregister") ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_STA); return fail_at("mode"); }
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *config) {
    assert(iface == WIFI_IF_STA && config->sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);
    assert(memcmp(config->sta.ssid, "host-test", 9) == 0);
    assert(config->sta.pmf_cfg.capable && !config->sta.pmf_cfg.required);
    return fail_at("config");
}
esp_err_t esp_wifi_start(void) {
    assert(handler != NULL); config_started = true;
    const esp_err_t result = fail_at("start");
    if (result == ESP_OK) { handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL); }
    return result;
}
esp_err_t esp_wifi_stop(void) { stops++; return is("stop-cleanup") ? ESP_FAIL : ESP_OK; }
esp_err_t esp_wifi_deinit(void) { deinit_calls++; return is("deinit-cleanup") ? ESP_FAIL : ESP_OK; }
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t depth, void *arg, unsigned priority, TaskHandle_t *handle) {
    assert(strcmp(name, "WIFI_MGR") == 0 && depth == 4096 && priority == 2 && arg == NULL && handle == NULL);
    tasks++; manager = fn;
    if (is("task") || strstr(scenario, "cleanup") != NULL || strstr(scenario, "unregister") != NULL) { return pdFALSE; }
    return pdPASS;
}
TickType_t xTaskGetTickCount(void) { return clock_ms; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) { assert(handle == NULL); return 2048; }
uint32_t esp_random(void) { return 42; }
uint32_t esp_get_free_heap_size(void) { return 100000; }
uint32_t esp_get_minimum_free_heap_size(void) { return 90000; }
bool YAMCS_SetLinkConnected(bool connected) { link_connected = connected; return true; }
bool YAMCS_LogInit(void) { log_calls++; return log_calls != 1; }
esp_err_t esp_wifi_connect(void) { assert(!WIFI_IsConnected()); connects++; return connects <= 2 ? ESP_FAIL : ESP_OK; }
esp_err_t esp_wifi_disconnect(void) {
    disconnects++; handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL); return ESP_OK;
}
static void got_ip(void) {
    ip_event_got_ip_t event = {.ip_info.ip.addr = 5};
    handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, &event);
    assert(WIFI_IsConnected());
}
void vTaskDelay(TickType_t ticks) {
    assert(real_runtime && ticks == 1000); clock_ms += ticks;
    if (clock_ms == 45000 || clock_ms == 180000 || clock_ms == 210000) { got_ip(); }
    if (clock_ms == 100000) {
        handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, NULL);
        assert(!WIFI_IsConnected() && !link_connected);
    }
    if (clock_ms == 160000) { handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_STOP, NULL); }
    if (clock_ms == 170000) { handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL); }
    if (clock_ms == 190000) {
        handler(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
        assert(!WIFI_IsConnected() && !link_connected);
    }
    if (clock_ms >= 300000) { longjmp(manager_exit, 1); }
}

int main(int argc, char **argv) {
    assert(argc == 2); scenario = argv[1];
    if (is("credentials")) { host_ssid = ""; }
    assert(!WIFI_IsConnected() && !WIFI_IsLogMirrorStarted());
    const esp_err_t result = WIFI_Start();
    assert(nvs_calls == 1 && WIFI_Start() == ESP_ERR_INVALID_STATE); // No full init retry.
    if (is("connected")) {
        assert(result == ESP_OK && WIFI_IsConnected() && link_connected && !WIFI_IsLogMirrorStarted());
        assert(tasks == 1 && !group.freed && destroys == 0);
    } else if (is("timeout") || is("runtime")) {
        assert(result == ESP_ERR_TIMEOUT && manager != NULL && tasks == 1 && !group.freed);
        assert(!WIFI_IsConnected() && clears == 0 && destroys == 0 && deinit_calls == 0);
        if (is("runtime")) {
            real_runtime = true;
            if (setjmp(manager_exit) == 0) { manager(NULL); assert(false); }
            assert(connects >= 6 && disconnects >= 1 && log_calls == 2);
            assert(WIFI_IsConnected() && WIFI_IsLogMirrorStarted() && link_connected);
            assert(!group.freed && destroys == 0 && unregisters == 0);
        }
    } else {
        assert(result != ESP_OK && result != ESP_ERR_TIMEOUT);
        if (is("nvs")) { assert(stack_calls == 0 && event_calls == 0); }
        if (is("stack")) { assert(event_calls == 0); }
        if (is("allocation") || is("existing") || is("credentials") || is("nvs") || is("stack") || is("event")) {
            assert(destroys == 0 && clears == 0 && deinit_calls == 0 && tasks == 0);
        } else if (is("stop-cleanup") || is("deinit-cleanup") || is("ip-unregister") || is("wifi-unregister") || is("clear-cleanup")) {
            assert(result == ESP_ERR_NO_MEM && destroys == 0 && !group.freed);
            assert(strstr(last_error, "retained") != NULL);
        } else {
            assert(destroys == 1 && clears == 1);
            if (!is("attach") && !is("defaults") && !is("group")) { assert(group.freed); }
            if (is("init")) { assert(deinit_calls == 0); }
            if (is("ip-handler")) { assert(unregisters == 1); }
            if (is("mode") || is("config") || is("start") || is("task")) { assert(unregisters == 2); }
        }
        if (!is("task") && strstr(scenario, "cleanup") == NULL && strstr(scenario, "unregister") == NULL) { assert(tasks == 0); }
        if (is("start") || is("task")) { assert(stops == 1 && config_started); }
    }
    printf("PASS: Wi-Fi service case %s; primary error/ownership/core-return policy preserved.\n", scenario);
    return 0;
}
