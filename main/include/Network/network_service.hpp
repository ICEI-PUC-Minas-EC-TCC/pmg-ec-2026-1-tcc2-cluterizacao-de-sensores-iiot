#pragma once

#include "Network/network_controller.hpp"
#include "esp_err.h"
#include <array>
#include <cstdint>
#include <vector>

namespace service::network {

enum class RxCommand : uint8_t {
    ACK = 0,
    PING,
    READING,
    ROTATE,

    SIZE,
};

enum class TxCommand : uint8_t {
    ACK = 0,
    PING,
    READING,
    ROTATE,

    SIZE,
};

struct __attribute__((packed)) ReadingPayload {
    float temperature;
    float current_ma;
    float battery_pct;
};

// Periodic broadcast carrying the sender's view of the current leader and
// (when energy-based policies are active) its residual energy.
// announced_leader is all-zero when the sender is UNDECIDED.
// Field order chosen so uint32_t residual_energy sits at offset 0, naturally
// aligned. With the previous layout (uint8_t[6] first) the uint32_t fell at
// offset 6, and packed accesses to that field broke ESP-NOW reception on
// ESP32-C3 (misaligned accesses inside the Wi-Fi/ESP-NOW path).
struct __attribute__((packed)) PingPayload {
    uint32_t residual_energy;
    uint8_t  announced_leader[6];
};

// Broadcast by the current leader when its term expires.
// Carries the MAC of the node that should assume leadership next.
struct __attribute__((packed)) RotatePayload {
    uint8_t next_leader[6];
};

void init();
void handler();

void ping_broadcast();
void ping_peer(controller::network::MacAddr dest_mac);
esp_err_t add_esp_peer(controller::network::MacAddr peer_mac, uint8_t peer_channel);

void send_reading(controller::network::MacAddr dest_mac, float temperature,
                  float current_ma, float battery_pct);
void send_rotate(controller::network::MacAddr next_leader);

const std::vector<controller::network::MacAddr>& get_known_peers();

bool has_received_reading();
float get_received_temperature();
float get_received_current_ma();
float get_received_battery_pct();
controller::network::MacAddr get_received_sender();

bool has_received_rotate();
controller::network::MacAddr get_rotate_next_leader();

} // namespace service::network