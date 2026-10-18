idf_component_register(
    SRCS 
        "deployment_service.c"
        "tc_proc.c"
	"tc_udp_rx.c"
        "cmd_inject.c"
        "cdhs_router.c"
	"data_logger.c"
	"eps_control.c"
	"tm_gen.c"
	"utils.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fdir hk lwip freertos system_mode comms fsw_common
                  esp_system adcs archive eps payload time watchdog recovery
)
