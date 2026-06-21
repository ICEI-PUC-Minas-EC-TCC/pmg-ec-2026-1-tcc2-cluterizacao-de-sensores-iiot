#pragma once

#include <array>
#include <cstdint>

namespace controller::network {

constexpr uint16_t MAX_MSG_ESP_NOW = 50u;

typedef std::array<uint8_t, 6> MacAddr;

void init();
void handler(void *arg);

void send_ping_broadcast();
void send_ping_device(MacAddr macAddr);

} // namespace controller::network