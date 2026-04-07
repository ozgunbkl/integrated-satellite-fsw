idf_component_register(
    SRCS 
	"payload_service.c"
	"payload_sim.c"
    INCLUDE_DIRS "."
	REQUIRES comms
)