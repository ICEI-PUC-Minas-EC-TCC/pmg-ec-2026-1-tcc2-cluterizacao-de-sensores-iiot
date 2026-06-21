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
    RESET_ENERGY,

    SIZE,
};

enum class TxCommand : uint8_t {
    ACK = 0,
    PING,
    READING,
    ROTATE,
    RESET_ENERGY,

    SIZE,
};

struct __attribute__((packed)) ReadingPayload {
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

enum class ResetScenario : uint8_t { FULL = 0, STAGGERED = 1 };

// Broadcast de reset de bancada. Carrega o cenario (cheio/escalonado) e o
// run_id compartilhado, para todos os nos iniciarem o mesmo ensaio.
struct __attribute__((packed)) ResetEnergyPayload {
    uint8_t scenario;
    char    run_id[24];
};

void init();
void handler();

void ping_broadcast();
void ping_peer(controller::network::MacAddr dest_mac);
esp_err_t add_esp_peer(controller::network::MacAddr peer_mac, uint8_t peer_channel);

void send_reading(controller::network::MacAddr dest_mac, float current_ma,
                  float battery_pct);
void send_rotate(controller::network::MacAddr next_leader);

// Broadcast a network-wide energy reset. Each peer that receives it erases its
// persisted energy (battery + budget) and restarts. Best-effort over ESP-NOW:
// the caller (button_service) repeats the broadcast a few times to cover loss.
void send_reset_energy_broadcast(ResetScenario scenario, const char *run_id);
ResetScenario get_reset_scenario();
const char *get_reset_run_id();

const std::vector<controller::network::MacAddr>& get_known_peers();

bool has_received_reading();
float get_received_current_ma();
float get_received_battery_pct();
controller::network::MacAddr get_received_sender();

bool has_received_rotate();
controller::network::MacAddr get_rotate_next_leader();

// True (once) after a RESET_ENERGY broadcast was received from a peer; the app
// then erases persisted energy and restarts. Mirrors has_received_rotate().
bool has_received_reset_energy();

} // namespace service::network