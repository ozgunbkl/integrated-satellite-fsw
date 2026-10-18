#include "wifi_service.h"
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "startup_nvs.h"
#include "startup_network.h"
#include "yamcs_interface.h"
#include "yamcs_log.h"
#include "link_retry.h"

#if (defined(FSW_TEST_WIFI_TIMEOUT) + defined(FSW_TEST_WIFI_LIFECYCLE) + \
     defined(FSW_TEST_WIFI_INTERFACE_FAILURE) + defined(FSW_TEST_NVS_INIT_FAILURE) + \
     defined(FSW_TEST_NETIF_INIT_FAILURE) + defined(FSW_TEST_EVENT_LOOP_FAILURE)) > 1
#error "Select exactly one explicit Wi-Fi/startup fault profile"
#endif

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#else
#define FSW_WIFI_SSID ""
#define FSW_WIFI_PASSWORD ""
#endif

#define CONNECTED_BIT BIT0
#define STARTED_BIT BIT1
#define DISCONNECTED_BIT BIT2
#define LOG_STARTED_BIT BIT3
#define LOG_ATTEMPTED_BIT BIT4
#define IP_RESET_BIT BIT5

static const char *TAG = "MAIN";
static EventGroupHandle_t s_events;
static esp_netif_t *s_station;
static esp_event_handler_instance_t s_wifi_handler, s_ip_handler;
static bool s_driver_initialized, s_start_attempted, s_start_called;

static uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

bool WIFI_IsConnected(void) {
    return s_events != NULL && (xEventGroupGetBits(s_events) & CONNECTED_BIT) != 0;
}

bool WIFI_IsLogMirrorStarted(void) {
    return s_events != NULL && (xEventGroupGetBits(s_events) & LOG_STARTED_BIT) != 0;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, STARTED_BIT);
    } else if ((base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) ||
               (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
            xEventGroupClearBits(s_events, STARTED_BIT);
        }
        xEventGroupSetBits(s_events, DISCONNECTED_BIT);
        if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
            xEventGroupSetBits(s_events, IP_RESET_BIT);
        }
        (void)YAMCS_SetLinkConnected(false); // Readiness of TCP/IP itself stays true.
        printf("WIFI: link unavailable; transmissions paused.\n");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP && data != NULL) {
        const ip_event_got_ip_t *event = data;
        if (!YAMCS_SetLinkConnected(true)) {
            ESP_LOGE(TAG, "IP readiness publication failed; transmissions remain paused.");
            return;
        }
        xEventGroupSetBits(s_events, CONNECTED_BIT);
        printf("MAIN: Wi-Fi connected. ESP32 IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t create_station(void) {
    const esp_netif_config_t config = ESP_NETIF_DEFAULT_WIFI_STA();
    if (esp_netif_get_handle_from_ifkey(config.base->if_key) != NULL) {
        return ESP_ERR_INVALID_STATE; // Never adopt/destroy a pre-existing station.
    }
    s_station = esp_netif_new(&config);
    if (s_station == NULL) {
        // SDK returns NULL for allocation/config errors, not a precise NO_MEM code.
        return ESP_FAIL;
    }
    esp_err_t result = esp_netif_attach_wifi_station(s_station);
    if (result == ESP_OK) { result = esp_wifi_set_default_wifi_sta_handlers(); }
    return result;
}

static void cleanup_startup(void) {
    // No manager exists on these failure paths. Stop producers before freeing
    // callback data. On uncertain SDK cleanup, retain resources rather than UAF.
    (void)YAMCS_SetLinkConnected(false);
    if (s_ip_handler != NULL) {
        if (esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, s_ip_handler) != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi cleanup IP unregister failed; resources retained.");
            return;
        }
        s_ip_handler = NULL;
    }
    if (s_wifi_handler != NULL) {
        if (esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, s_wifi_handler) != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi cleanup event unregister failed; resources retained.");
            return;
        }
        s_wifi_handler = NULL;
    }
    // Unregister waits for in-flight application callbacks before stopping the
    // driver. A queued old GOT_IP must not reopen the sender after cleanup.
    (void)YAMCS_SetLinkConnected(false);
    if (s_driver_initialized && s_start_attempted) {
        const esp_err_t result = esp_wifi_stop();
        if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED && result != ESP_ERR_WIFI_NOT_INIT) {
            ESP_LOGE(TAG, "Wi-Fi cleanup stop failed (%s); resources retained.", esp_err_to_name(result));
            return;
        }
    }
    if (s_driver_initialized) {
        const esp_err_t result = esp_wifi_deinit();
        if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_INIT) {
            ESP_LOGE(TAG, "Wi-Fi cleanup deinit failed (%s); resources retained.", esp_err_to_name(result));
            return;
        }
        s_driver_initialized = false;
    }
    if (s_station != NULL) {
        const esp_err_t result = esp_wifi_clear_default_wifi_driver_and_handlers(s_station);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi cleanup interface failed (%s); resources retained.", esp_err_to_name(result));
            return;
        }
        esp_netif_destroy(s_station);
        s_station = NULL;
    }
    if (s_events != NULL) { vEventGroupDelete(s_events); s_events = NULL; }
    // The initialized TCP/IP stack/default event loop intentionally remain alive.
}

#ifdef FSW_TEST_WIFI_LIFECYCLE
static wifi_config_t s_real_config;
static void lifecycle_test(uint32_t elapsed, LinkRetry *retry) {
    static bool restored;
    static unsigned disconnects;
    if (!restored && elapsed >= 22000) {
        const esp_err_t off = esp_wifi_disconnect();
        if (off != ESP_OK && off != ESP_ERR_WIFI_NOT_CONNECT) {
            printf("TEST ERROR: lifecycle disconnect failed.\n");
            return;
        }
        if (esp_wifi_set_config(WIFI_IF_STA, &s_real_config) != ESP_OK) {
            printf("TEST ERROR: lifecycle configuration restore failed.\n");
            return;
        }
        restored = true;
        LINK_Init(retry, now_ms());
        printf("TEST: valid Wi-Fi configuration restored in RAM after late startup.\n");
    }
    if (restored && disconnects < 3 && elapsed >= 90000 + disconnects * 60000 && WIFI_IsConnected()) {
        if (esp_wifi_disconnect() != ESP_OK) {
            printf("TEST ERROR: lifecycle real disconnect failed.\n");
            return;
        }
        disconnects++;
        printf("TEST: real station disconnect %u/3 requested.\n", disconnects);
    }
}
#endif

static void wifi_manager_task(void *argument) {
    (void)argument;
    LinkRetry retry;
    LINK_Init(&retry, now_ms());
    uint32_t log_retry = now_ms(), health_at = now_ms();
#ifdef FSW_TEST_WIFI_LIFECYCLE
    const uint32_t test_start = now_ms();
#endif
    printf("WIFI: reconnect manager running (1..30 second backoff).\n");
    for (;;) {
        const uint32_t now = now_ms();
        const EventBits_t bits = xEventGroupWaitBits(s_events, DISCONNECTED_BIT | IP_RESET_BIT, pdTRUE, pdFALSE, 0);
        const bool connected = (bits & CONNECTED_BIT) != 0;
        LinkAction action;
        if ((bits & IP_RESET_BIT) != 0 && !connected) {
            LINK_Init(&retry, now);
            LINK_Failed(&retry, now);
            action = LINK_DISCONNECT; // Lost DHCP/IP needs a fresh association.
        } else {
            action = LINK_Step(&retry, now, (bits & STARTED_BIT) != 0,
                                connected, (bits & DISCONNECTED_BIT) != 0);
        }
        if (action != LINK_IDLE) {
            const esp_err_t result = action == LINK_CONNECT ? esp_wifi_connect() : esp_wifi_disconnect();
            if (result != ESP_OK) {
                ESP_LOGW(TAG, "Wi-Fi connection request failed (%s); bounded retry continues.", esp_err_to_name(result));
                if (action == LINK_CONNECT) { LINK_Failed(&retry, now); }
            }
        }
        if (connected && !WIFI_IsLogMirrorStarted() && (int32_t)(now - log_retry) >= 0) {
            const bool started = YAMCS_LogInit(); // This task is the sole lifecycle owner.
            xEventGroupSetBits(s_events, LOG_ATTEMPTED_BIT | (started ? LOG_STARTED_BIT : 0));
            if (started) { printf("WIFI: YAMCS log mirror started after IP readiness.\n"); }
            else { ESP_LOGW(TAG, "Log mirror unavailable; retry in 30 seconds; core continues."); }
            log_retry = now + 30000;
        }
        if ((int32_t)(now - health_at) >= 0) {
            // ESP-IDF Xtensa StackType_t is uint8_t: high-water units are bytes.
            printf("WIFI: health connected=%u attempts=%lu heap=%lu min_heap=%lu stack_free_bytes=%lu\n",
                   (unsigned)connected, (unsigned long)retry.attempts,
                   (unsigned long)esp_get_free_heap_size(), (unsigned long)esp_get_minimum_free_heap_size(),
                   (unsigned long)uxTaskGetStackHighWaterMark(NULL));
            health_at = now + 30000;
        }
#ifdef FSW_TEST_WIFI_LIFECYCLE
        lifecycle_test(now - test_start, &retry);
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t WIFI_Start(void) {
    if (s_start_called) { return ESP_ERR_INVALID_STATE; }
    s_start_called = true;
    esp_err_t result = STARTUP_InitNvs();
    if (result != ESP_OK) { return result; }
    result = STARTUP_InitNetworkStack();
    if (result != ESP_OK) { return result; }
    result = STARTUP_CreateWifiEventLoop();
    if (result != ESP_OK) { return result; }

    wifi_config_t config = { .sta = { .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                                     .pmf_cfg = {.capable = true, .required = false} } };
#ifndef FSW_TEST_WIFI_TIMEOUT
    const size_t ssid_len = strlen(FSW_WIFI_SSID), password_len = strlen(FSW_WIFI_PASSWORD);
    if (ssid_len == 0 || ssid_len > sizeof(config.sta.ssid) || password_len > sizeof(config.sta.password) || password_len < 8) {
        ESP_LOGE(TAG, "Wi-Fi local configuration missing/invalid; credentials are never logged.");
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(config.sta.ssid, FSW_WIFI_SSID, ssid_len);
    memcpy(config.sta.password, FSW_WIFI_PASSWORD, password_len);
#endif
#ifdef FSW_TEST_WIFI_LIFECYCLE
    s_real_config = config;
#endif
#if defined(FSW_TEST_WIFI_TIMEOUT) || defined(FSW_TEST_WIFI_LIFECYCLE)
    memset(config.sta.ssid, 0, sizeof(config.sta.ssid));
    memset(config.sta.password, 0, sizeof(config.sta.password));
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "FSW_NO_AP_%08lx", (unsigned long)esp_random());
    memcpy(config.sta.password, "offline-test-only", sizeof("offline-test-only"));
#endif
    const char *stage = "station interface";
    result = create_station();
    if (result != ESP_OK) { goto failed; }
    stage = "event group";
    s_events = xEventGroupCreate();
    if (s_events == NULL) { result = ESP_ERR_NO_MEM; goto failed; }
    stage = "driver init";
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    result = esp_wifi_init(&init);
    if (result != ESP_OK) { goto failed; }
    s_driver_initialized = true;
    stage = "RAM storage";
    result = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (result != ESP_OK) { goto failed; }
    stage = "Wi-Fi handler";
    result = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &s_wifi_handler);
    if (result != ESP_OK) { goto failed; }
    stage = "IP handler";
    result = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, &s_ip_handler);
    if (result != ESP_OK) { goto failed; }
    stage = "station mode";
    result = esp_wifi_set_mode(WIFI_MODE_STA);
    if (result != ESP_OK) { goto failed; }
    stage = "station configuration";
    result = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (result != ESP_OK) { goto failed; }
    stage = "driver start";
    s_start_attempted = true;
    result = esp_wifi_start();
    if (result != ESP_OK) { goto failed; }
    stage = "reconnect task";
    if (xTaskCreate(wifi_manager_task, "WIFI_MGR", 4096, NULL, 2, NULL) != pdPASS) {
        result = ESP_ERR_NO_MEM; goto failed;
    }
#ifdef FSW_TEST_WIFI_TIMEOUT
    printf("TEST: Wi-Fi timeout profile active; temporary settings use RAM.\n");
#endif
#ifdef FSW_TEST_WIFI_LIFECYCLE
    printf("TEST: Wi-Fi lifecycle profile active; late connection and 3 real disconnects.\n");
#endif
    const EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT | LOG_ATTEMPTED_BIT,
                                                pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
    if ((bits & CONNECTED_BIT) == 0) {
        ESP_LOGE(TAG, "Wi-Fi did not connect within timeout");
        return ESP_ERR_TIMEOUT; // Do not tear down: later IP is handled by the manager.
    }
    return ESP_OK;

failed:
    ESP_LOGE(TAG, "Wi-Fi startup failed at %s (%s); Wi-Fi skipped; core startup continues.", stage, esp_err_to_name(result));
    cleanup_startup();
    return result; // Cleanup never overwrites the primary error.
}
