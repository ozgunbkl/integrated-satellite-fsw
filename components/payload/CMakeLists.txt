idf_component_register(
    SRCS 
	"payload_service.c"
	"payload_sim.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES comms fdir eps freertos archive time fsw_common
)
