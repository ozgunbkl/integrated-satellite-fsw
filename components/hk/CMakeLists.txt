idf_component_register(
    SRCS "hk_service.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES adcs eps comms freertos lwip fsw_common
)
