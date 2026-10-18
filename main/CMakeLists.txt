idf_component_register(
    SRCS "main.c" "resource_monitor.c" "fdir_task.c" "eps_harvest.c" "startup_core.c" "startup_resources.c" "startup_nvs.c" "nvs_fault_injection.c"
         "startup_network.c" "netif_fault_injection.c"
         "event_loop_fault_injection.c" "startup_fault_injection.c" "mission_lifecycle_test.c" "recovery_app.c"
         "wifi_service.c" "wifi_fault_injection.c"
    INCLUDE_DIRS "."
    REQUIRES freertos esp_common
    PRIV_REQUIRES adcs archive hk payload comms time esp_wifi esp_event esp_netif fdir
                  nvs_flash lwip heap system_mode cdh eps fsw_common watchdog recovery esp_system
)
if(EXISTS "${CMAKE_SOURCE_DIR}/local/wifi_credentials.h")
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_SOURCE_DIR}/local")
endif()
