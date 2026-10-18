#include "time_port.h"

#include "esp_timer.h"

void TIME_PortInit(void) {
    // ESP Timer is initialized by ESP-IDF before app_main() starts.
}

uint64_t TIME_PortGetMicroseconds(void) {
    return (uint64_t)esp_timer_get_time();
}
