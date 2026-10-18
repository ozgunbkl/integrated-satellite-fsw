#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "archive_port.h"
#include "esp_random.h"
static unsigned calls, zeros;
void esp_fill_random(void *buffer, size_t size) {
    assert(size == sizeof(uint64_t)); ++calls;
    const uint64_t value = calls <= zeros ? 0 : UINT64_C(0xFEDCBA9876543210);
    memcpy(buffer, &value, size);
}
int main(void) {
    uint64_t boot_id = 123;
    assert(!Archive_PortCreateBootId(NULL) && calls == 0);
    assert(Archive_PortCreateBootId(&boot_id) && calls == 1 && boot_id == UINT64_C(0xFEDCBA9876543210));
    calls = 0; zeros = 3; boot_id = 123;
    assert(Archive_PortCreateBootId(&boot_id) && calls == 4 && boot_id != 0);
    calls = 0; zeros = 4; boot_id = 123;
    assert(!Archive_PortCreateBootId(&boot_id) && calls == 4 && boot_id == 123);
    puts("PASS: boot-discriminator adapter null, nonzero success, bounded zero retry/failure; no NVS or radio change.");
    return 0;
}
