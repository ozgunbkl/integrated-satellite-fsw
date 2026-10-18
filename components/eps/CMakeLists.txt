idf_component_register(
    SRCS 
        "eps_commands.c"
        "eps_logic.c"
    INCLUDE_DIRS "."
    REQUIRES freertos system_mode
    PRIV_REQUIRES fdir time watchdog fsw_common
)
