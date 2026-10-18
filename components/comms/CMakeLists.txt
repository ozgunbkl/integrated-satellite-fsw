idf_component_register(
    SRCS 
	"ccsds_packet.c"
	"yamcs_interface.c"
	"yamcs_log.c"
	"link_retry.c"
    INCLUDE_DIRS "."
    REQUIRES fsw_common
    PRIV_REQUIRES lwip freertos time
)
