idf_component_register(
    SRCS 
        "archive_service.c"
        "archive_port_esp32.c"
        "archive_transfer_wire.c"
        "utils.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fdir comms fsw_common time freertos esp_hw_support
)
