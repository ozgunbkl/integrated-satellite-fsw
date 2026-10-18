#include "time_service.h"
#include "time_port.h"

static uint64_t s_met_start_us = 0;

static uint64_t time_get_elapsed_us(void) {
    const uint64_t now_us = TIME_PortGetMicroseconds();

    if (now_us < s_met_start_us) {
        return 0;
    }

    return now_us - s_met_start_us;
}

void TIME_Init(void) {
    TIME_PortInit();
    s_met_start_us = TIME_PortGetMicroseconds();
}

uint32_t TIME_GetSeconds(void) {
    return (uint32_t)(time_get_elapsed_us() / 1000000ULL);
}

uint64_t TIME_GetMilliseconds(void) {
    return time_get_elapsed_us() / 1000ULL;
}
