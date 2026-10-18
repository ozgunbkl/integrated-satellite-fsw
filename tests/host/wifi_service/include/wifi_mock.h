#ifndef WIFI_MOCK_H
#define WIFI_MOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BIT0 1u
#define BIT1 2u
#define BIT2 4u
#define BIT3 8u
#define BIT4 16u
#define BIT5 32u
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_WIFI_NOT_INIT 0x3001
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
const char *esp_err_to_name(esp_err_t code);
void host_log(const char *tag, const char *format, ...);
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
typedef int BaseType_t;
typedef uint32_t TickType_t, EventBits_t, UBaseType_t;
typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;
typedef struct HostEvent *EventGroupHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t depth,
                       void *arg, unsigned priority, TaskHandle_t *handle);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
#define taskSCHEDULER_RUNNING 2
BaseType_t xTaskGetSchedulerState(void);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle);
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t group);
EventBits_t xEventGroupGetBits(EventGroupHandle_t group);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits,
                               BaseType_t clear, BaseType_t all, TickType_t wait);
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
extern const char WIFI_EVENT[], IP_EVENT[];
#define ESP_EVENT_ANY_ID -1
enum { WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP, WIFI_EVENT_STA_DISCONNECTED,
       IP_EVENT_STA_GOT_IP, IP_EVENT_STA_LOST_IP };
typedef struct { struct { struct { unsigned long addr; } ip; } ip_info; } ip_event_got_ip_t;
#define IPSTR "%lu"
#define IP2STR(ip) (ip)->addr
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
                  esp_event_handler_t fn, void *arg, esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
                                               esp_event_handler_instance_t instance);
typedef struct { const char *if_key; } esp_netif_inherent_config_t;
typedef struct { const esp_netif_inherent_config_t *base; } esp_netif_config_t;
typedef struct { int value; } esp_netif_t;
extern const esp_netif_inherent_config_t host_netif_config;
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){.base = &host_netif_config})
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key);
esp_netif_t *esp_netif_new(const esp_netif_config_t *config);
void esp_netif_destroy(esp_netif_t *netif);
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *netif);
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void);
esp_err_t esp_wifi_clear_default_wifi_driver_and_handlers(void *netif);
typedef struct { int magic; } wifi_init_config_t;
typedef struct { struct {
    uint8_t ssid[32], password[64];
    struct { int authmode; } threshold;
    struct { bool capable, required; } pmf_cfg;
} sta; } wifi_config_t;
#define WIFI_AUTH_WPA2_PSK 1
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 1
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){.magic = 42})
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *config);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
uint32_t esp_random(void);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
extern const char *host_ssid;
#endif
