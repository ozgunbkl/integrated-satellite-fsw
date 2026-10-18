idf_component_register(
    SRCS "recovery.c" "recovery_port_esp32.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES freertos nvs_flash esp_system system_mode fsw_common
)
