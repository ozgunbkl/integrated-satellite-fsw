idf_component_register(
    SRCS "watchdog.c" "watchdog_port_esp32.c" "watchdog_test_esp32.c" "watchdog_global_test_esp32.c"
         "watchdog_recovery_test_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES fsw_common
    PRIV_REQUIRES fdir time system_mode freertos esp_system
)
