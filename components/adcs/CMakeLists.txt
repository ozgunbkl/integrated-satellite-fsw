idf_component_register(
    SRCS 
        "adcs_manager.c"
        "adcs_service.c"
        "adcs_sim.c"
	"adcs_control.c"
	"adcs_math.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES fdir system_mode eps freertos time watchdog fsw_common
)
