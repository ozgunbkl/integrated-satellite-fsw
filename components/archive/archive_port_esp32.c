#include "archive_port.h"
#include "esp_random.h"

bool Archive_PortCreateBootId(uint64_t *boot_id) {
    if (boot_id == NULL) return false;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        uint64_t candidate;
        esp_fill_random(&candidate, sizeof(candidate));
        if (candidate != 0) { *boot_id = candidate; return true; }
    }
    return false;
}
