#ifdef FSW_TEST_WIFI_INTERFACE_FAILURE
#include <stdio.h>
#include <stdlib.h>
#include "esp_netif.h"
#include "esp_wifi.h"
esp_netif_t *__wrap_esp_netif_new(const esp_netif_config_t *config) {
    (void)config;
    printf("TEST: Wi-Fi station allocation failure injected; real heap remains intact.\n");
    return NULL;
}
esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config) {
    (void)config;
    printf("TEST ERROR: driver init after failed station allocation.\n");
    abort();
}
#endif
