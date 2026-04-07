idf_component_register(
    SRCS 
        "deployment_service.c"
        "tc_proc.c"
	"tc_udp_rx.c"
        "test_state_manager.c"
        "cmd_inject.c"
        "cdhs_router.c"
	"data_logger.c"
	"eps_control.c"
	"tm_gen.c"
	"tm_manager.c"
	"utils.c"
	"watchdog.c"
    INCLUDE_DIRS "."
    REQUIRES hk lwip freertos
)