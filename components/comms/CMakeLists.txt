idf_component_register(
    SRCS 
	"ccsds_packet.c"
	"comms_frame.c"
	"yamcs_interface.c"
	"yamcs_log.c"
    INCLUDE_DIRS "."
	REQUIRES lwip freertos
)