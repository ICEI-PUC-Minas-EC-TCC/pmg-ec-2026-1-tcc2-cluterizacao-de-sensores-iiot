#pragma once
#include <cstdint>
#include <cstring>
typedef int wifi_interface_t;
#define WIFI_IF_STA 0
extern uint8_t g_oracle_own_mac[6];
inline int esp_wifi_get_mac(wifi_interface_t, uint8_t *out) {
    memcpy(out, g_oracle_own_mac, 6); return 0;
}
