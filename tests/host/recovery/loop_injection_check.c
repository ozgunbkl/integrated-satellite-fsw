/* Real test-only wrapper, mocked journal/SDK. No flash, reset or fault clear. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "recovery.h"
#include "startup_fault_test.h"

extern bool __wrap_HK_Init(void);
static RecoverySnapshot_t state = {.boot_count=8, .flags=35, .storage_ok=true};
static esp_reset_reason_t reason = ESP_RST_POWERON;
static bool available = true, real_result = true;
static unsigned real_calls;
static char output[2048];
static size_t used;
bool RECOVERY_GetSnapshot(RecoverySnapshot_t *out) { *out=state; return available; }
esp_reset_reason_t esp_reset_reason(void) { return reason; }
bool __real_HK_Init(void) { ++real_calls; return real_result; }
int __wrap_printf(const char *format, ...) {
    va_list args; va_start(args, format);
    int n=vsnprintf(output+used, sizeof(output)-used, format, args); va_end(args);
    assert(n>=0 && (size_t)n<sizeof(output)-used); used+=(size_t)n; return n;
}
int main(int argc, char **argv) {
    assert(argc==2);
    const char *name=argv[1];
    if (strcmp(name,"failed_1")==0 || strcmp(name,"failed_2")==0 || strcmp(name,"failed_3")==0 ||
            strcmp(name,"failed_4")==0 || strcmp(name,"real_failure")==0) {
        state.failed_boots = strcmp(name,"real_failure")==0 ? 3U : (unsigned)(name[7]-'0');
        state.boot_count += state.failed_boots; reason=ESP_RST_PANIC;
        if (state.failed_boots>=3U) state.flags|=RECOVERY_BOOT_LOOP;
    }
    if (strcmp(name,"snapshot")==0) available=false;
    if (strcmp(name,"storage")==0) state.storage_ok=false;
    if (strcmp(name,"stable")==0) state.stable=true;
    if (strcmp(name,"zero_boot")==0) state.boot_count=0;
    if (strcmp(name,"no_open")==0) state.flags&=~RECOVERY_OPEN_BOOT;
    if (strcmp(name,"fatal")==0) state.flags|=RECOVERY_FATAL;
    if (strcmp(name,"early_loop")==0) state.flags|=RECOVERY_BOOT_LOOP;
    if (strcmp(name,"wrong_first_reason")==0) reason=ESP_RST_PANIC;
    if (strcmp(name,"wrong_later_reason")==0) { state.failed_boots=1; reason=ESP_RST_SW; }
    state.reset_reason=(unsigned)reason;
    if (strcmp(name,"reason_mismatch")==0) ++state.reset_reason;
    if (strcmp(name,"real_failure")==0) real_result=false;
    const bool armed=strcmp(name,"failed_0")==0 || strcmp(name,"failed_1")==0 || strcmp(name,"failed_2")==0;
    const bool suppressed=strcmp(name,"failed_3")==0 || strcmp(name,"real_failure")==0;
    const bool result=__wrap_HK_Init();
    assert(result==(armed ? false : real_result));
    assert(real_calls==(armed ? 0U : 1U));
    if (armed || suppressed) {
        assert(strstr(output,"TEST ERROR:")==NULL);
        assert(strstr(output,armed ? "RECOVERY_LOOP injection=armed" : "RECOVERY_LOOP injection=suppressed"));
        assert((strstr(output,"real_call=skipped")!=NULL)==armed);
    } else {
        assert(strstr(output,"TEST ERROR: Recovery boot-loop injection refused invalid journal"));
        assert(strstr(output,"real_call=skipped")==NULL);
    }
    puts("PASS: bounded three-failure injection/caller passthrough; mocked journal/SDK, no hardware.");
    return 0;
}
