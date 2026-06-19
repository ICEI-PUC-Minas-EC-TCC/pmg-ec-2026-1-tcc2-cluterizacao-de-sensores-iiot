#include "Network/network_service.hpp"
#include "Application/energy_service.hpp"
#include "Application/role_service.hpp"
#include "Network/esp_now_driver.hpp"
#include "Network/network_controller.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace service::network {

using controller::network::MacAddr;

using driver::network::esp_now::Packet;
using driver::network::esp_now::register_rx_callback;

static std::vector<MacAddr> known_peers;

static bool reading_available = false;
static float received_current_ma = 0.0f;
static float received_battery_pct = 0.0f;
static MacAddr received_sender{};

static bool rotate_available = false;
static MacAddr rotate_next_leader{};

static bool reset_energy_available = false;
static ResetScenario reset_scenario = ResetScenario::FULL;
static char reset_run_id[24] = "UNKNOWN";

void ping_received(Packet packet);
void reading_received(Packet packet);
void rotate_received(Packet packet);
void reset_energy_received(Packet packet);

void init() {
    register_rx_callback(ping_received, RxCommand::PING);
    register_rx_callback(reading_received, RxCommand::READING);
    register_rx_callback(rotate_received, RxCommand::ROTATE);
    register_rx_callback(reset_energy_received, RxCommand::RESET_ENERGY);
}

void handler() {
    driver::network::esp_now::handler();
}

// Registers the sender as a peer on the first message of any kind from it,
// so that discovery does not depend exclusively on broadcast PINGs (which
// are lossier than unicast frames in Wi-Fi).
static void register_sender_if_new(const uint8_t *src_mac) {
    MacAddr sender;
    memcpy(sender.data(), src_mac, sizeof(sender));

    if (add_esp_peer(sender, 0) == ESP_OK) {
        known_peers.push_back(sender);
    }
}

void ping_received(Packet packet) {
    register_sender_if_new(packet.src_mac);

    ESP_LOGI(__FUNCTION__, "Package received succesfull");

    PingPayload payload{};
    memcpy(&payload, packet.data, sizeof(payload));

    MacAddr announced{};
    memcpy(announced.data(), payload.announced_leader, sizeof(announced));

    MacAddr sender{};
    memcpy(sender.data(), packet.src_mac, sizeof(sender));

    service::application::role::on_leader_announced(announced);
    service::application::energy::on_peer_energy(sender,
                                                 payload.residual_energy);
}

static std::array<uint8_t, sizeof(PingPayload)> build_ping_payload() {
    PingPayload payload{};
    MacAddr announced = service::application::role::get_announced_leader();
    memcpy(payload.announced_leader, announced.data(),
           sizeof(payload.announced_leader));
    payload.residual_energy = service::application::energy::get_residual();

    std::array<uint8_t, sizeof(PingPayload)> data{};
    memcpy(data.data(), &payload, sizeof(payload));
    return data;
}

void ping_peer(MacAddr dest_mac) {
    auto data = build_ping_payload();
    driver::network::esp_now::send_msg(RxCommand::PING, dest_mac, data);
}

void ping_broadcast() {
    auto data = build_ping_payload();
    driver::network::esp_now::send_broadcast(RxCommand::PING, data);
}

esp_err_t add_esp_peer(MacAddr peer_mac, uint8_t peer_channel) {
    esp_err_t ret =
        driver::network::esp_now::add_peer(peer_mac.data(), peer_channel);

    if (ret == ESP_OK) {
        ESP_LOGI(__FUNCTION__, "New peer: " MACSTR, MAC2STR(peer_mac.data()));
    } else if (ret == ESP_ERR_ESPNOW_EXIST) {
    } else {
        ESP_LOGE(__FUNCTION__, "Failed: %u", ret);
    }

    return ret;
}

const std::vector<MacAddr> &get_known_peers() {
    return known_peers;
}

void reading_received(Packet packet) {
    register_sender_if_new(packet.src_mac);

    ReadingPayload payload{};
    memcpy(&payload, packet.data, sizeof(payload));

    received_current_ma = payload.current_ma;
    received_battery_pct = payload.battery_pct;
    memcpy(received_sender.data(), packet.src_mac, sizeof(received_sender));
    reading_available = true;
}

bool has_received_reading() {
    if (reading_available) {
        reading_available = false;
        return true;
    }
    return false;
}

float get_received_current_ma() {
    return received_current_ma;
}

float get_received_battery_pct() {
    return received_battery_pct;
}

MacAddr get_received_sender() {
    return received_sender;
}

void send_reading(MacAddr dest_mac, float current_ma, float battery_pct) {
    ReadingPayload payload{.current_ma = current_ma,
                           .battery_pct = battery_pct};

    std::array<uint8_t, sizeof(ReadingPayload)> data{};
    memcpy(data.data(), &payload, sizeof(payload));

    driver::network::esp_now::send_msg(RxCommand::READING, dest_mac, data);
}

void rotate_received(Packet packet) {
    register_sender_if_new(packet.src_mac);

    RotatePayload payload{};
    memcpy(&payload, packet.data, sizeof(payload));

    memcpy(rotate_next_leader.data(), payload.next_leader,
           sizeof(rotate_next_leader));
    rotate_available = true;
}

bool has_received_rotate() {
    if (rotate_available) {
        rotate_available = false;
        return true;
    }
    return false;
}

MacAddr get_rotate_next_leader() {
    return rotate_next_leader;
}

void send_rotate(MacAddr next_leader) {
    RotatePayload payload{};
    memcpy(payload.next_leader, next_leader.data(),
           sizeof(payload.next_leader));

    std::array<uint8_t, sizeof(RotatePayload)> data{};
    memcpy(data.data(), &payload, sizeof(payload));

    // Unicast (not broadcast): broadcasts in 2.4 GHz drop heavily in lossy
    // links because they lack MAC-layer ACK/retry. Sending ROTATE directly
    // to the elected next leader leverages ESP-NOW's hardware retry and
    // makes rotation reliable even when one node is channel-hopping due to
    // Wi-Fi reconnect backoff.
    driver::network::esp_now::send_msg(RxCommand::ROTATE, next_leader, data);
}

void reset_energy_received(Packet packet) {
    register_sender_if_new(packet.src_mac);
    ResetEnergyPayload payload{};
    memcpy(&payload, packet.data, sizeof(payload));
    reset_scenario = (payload.scenario == (uint8_t)ResetScenario::STAGGERED)
                         ? ResetScenario::STAGGERED
                         : ResetScenario::FULL;
    payload.run_id[sizeof(payload.run_id) - 1] = '\0';
    strncpy(reset_run_id, payload.run_id, sizeof(reset_run_id) - 1);
    reset_run_id[sizeof(reset_run_id) - 1] = '\0';
    reset_energy_available = true;
}

bool has_received_reset_energy() {
    if (reset_energy_available) {
        reset_energy_available = false;
        return true;
    }
    return false;
}

void send_reset_energy_broadcast(ResetScenario scenario, const char *run_id) {
    ResetEnergyPayload payload{};
    payload.scenario = (uint8_t)scenario;
    strncpy(payload.run_id, run_id, sizeof(payload.run_id) - 1);
    payload.run_id[sizeof(payload.run_id) - 1] = '\0';

    std::array<uint8_t, sizeof(ResetEnergyPayload)> data{};
    memcpy(data.data(), &payload, sizeof(payload));
    driver::network::esp_now::send_broadcast(RxCommand::RESET_ENERGY, data);
}

ResetScenario get_reset_scenario() { return reset_scenario; }
const char *get_reset_run_id() { return reset_run_id; }

} // namespace service::network
