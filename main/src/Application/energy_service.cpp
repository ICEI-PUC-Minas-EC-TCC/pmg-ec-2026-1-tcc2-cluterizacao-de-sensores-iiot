#include "Application/energy_service.hpp"
#include "esp_log.h"
#include "utils.hpp"

#include <cstring>
#include <unordered_map>

static const char *TAG = "ENERGY_SERVICE";

namespace service::application::energy {

using controller::network::MacAddr;

// Initial budget in abstract units. Costs are calibrated relative to this.
static constexpr uint32_t INITIAL_BUDGET   = 100'000;

// Per-operation costs (abstract units). Calibrated by order-of-magnitude
// from ESP32-C3 datasheet: TX Wi-Fi > TX ESP-NOW > idle.
static constexpr uint32_t COST_MQTT        = 50;
static constexpr uint32_t COST_ESPNOW_SEND = 5;
static constexpr uint32_t COST_TICK        = 1;

// Idle decay cadence.
static constexpr uint32_t TICK_PERIOD_MS   = 1000;

// Peer energy samples expire after this window. Stale samples are reported
// as invalid so the leader policy can fall back to round-robin.
static constexpr uint32_t PEER_TTL_MS      = 10'000;

static uint32_t      residual = INITIAL_BUDGET;
static utils::Timer  tick_timer;

struct PeerSample {
    uint32_t       residual;
    utils::Timer   age;
};

static std::unordered_map<uint64_t, PeerSample> peers;

static uint64_t mac_to_key(MacAddr mac) {
    uint64_t k = 0;
    for (int i = 0; i < 6; ++i) {
        k = (k << 8) | mac[i];
    }
    return k;
}

static void decrement(uint32_t cost) {
    if (residual <= cost) {
        residual = 0;
    } else {
        residual -= cost;
    }
}

void init() {
    residual = INITIAL_BUDGET;
    peers.clear();
    tick_timer.reset();
    ESP_LOGI(TAG, "Energy budget initialized: %u units", (unsigned)residual);
}

void on_mqtt_publish() {
    decrement(COST_MQTT);
}

void on_espnow_send() {
    decrement(COST_ESPNOW_SEND);
}

void tick() {
    if (!tick_timer.hasElapsed(TICK_PERIOD_MS)) {
        return;
    }
    tick_timer.reset();
    decrement(COST_TICK);
}

uint32_t get_residual() {
    return residual;
}

void on_peer_energy(MacAddr peer, uint32_t peer_residual) {
    uint64_t key = mac_to_key(peer);
    auto &sample = peers[key];
    sample.residual = peer_residual;
    sample.age.reset();
}

uint32_t get_peer_residual(MacAddr peer, bool *valid) {
    uint64_t key = mac_to_key(peer);
    auto it = peers.find(key);
    if (it == peers.end() || it->second.age.hasElapsed(PEER_TTL_MS)) {
        if (valid) *valid = false;
        return 0;
    }
    if (valid) *valid = true;
    return it->second.residual;
}

} // namespace service::application::energy
