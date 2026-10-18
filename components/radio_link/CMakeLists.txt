# Existing demo radio framing + mission dispatch, not the active UDP transport.
idf_component_register(
    SRCS "comms_frame.c" "tm_manager.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES comms cdh
)
