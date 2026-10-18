idf_component_register(
    SRCS "fdir_service.c"
    INCLUDE_DIRS "."
    REQUIRES system_mode
    PRIV_REQUIRES comms freertos time fsw_common
)
