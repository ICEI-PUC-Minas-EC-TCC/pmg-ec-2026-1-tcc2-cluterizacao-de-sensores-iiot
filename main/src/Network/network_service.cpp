#include "Network/network_service.hpp"
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

namespace service::network {

using controller::network::MacAddr;

using driver::network::esp_now::Packet;
using driver::network::esp_now::register_rx_callback;

static std::vector<MacAddr> known_peers;

static bool  reading_available    = false;
static float received_temperature = 0.0f;
static MacAddr received_sender{};

void ping_received(Packet packet);
void reading_received(Packet packet);

void init() {
    register_rx_callback(ping_received,    RxCommand::PING);
    register_rx_callback(reading_received, RxCommand::READING);
}

void handler() {
    driver::network::esp_now::handler();
}

void ping_received(Packet packet) {
    MacAddr sender;
    memcpy(sender.data(), packet.src_mac, sizeof(sender));

    if (add_esp_peer(sender, 0) == ESP_OK) {
        known_peers.push_back(sender);
        service::application::role::on_peer_discovered();
    }

    ESP_LOGI(__FUNCTION__, "Ping received from " MACSTR, MAC2STR(packet.src_mac));
}

void ping_peer(MacAddr dest_mac) {
    std::array<uint8_t, 1> data = {};
    driver::network::esp_now::send_msg(RxCommand::PING, dest_mac, data);
}

void ping_broadcast() {
    std::array<uint8_t, 1> data = {};
    driver::network::esp_now::send_broadcast(RxCommand::PING, data);
}

esp_err_t add_esp_peer(MacAddr peer_mac, uint8_t peer_channel) {
    esp_err_t ret = driver::network::esp_now::add_peer(peer_mac.data(), peer_channel);

    if (ret == ESP_OK) {
        ESP_LOGI(__FUNCTION__, "New peer: " MACSTR, MAC2STR(peer_mac.data()));
    } else if (ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(__FUNCTION__, "Failed to add peer: %u", ret);
    }

    return ret;
}

const std::vector<MacAddr>& get_known_peers() {
    return known_peers;
}

void reading_received(Packet packet) {
    ReadingPayload payload{};
    memcpy(&payload, packet.data, sizeof(payload));

    received_temperature = payload.temperature;
    memcpy(received_sender.data(), packet.src_mac, sizeof(received_sender));
    reading_available = true;

    ESP_LOGI(__FUNCTION__, "Reading from " MACSTR ": %.1f C",
             MAC2STR(packet.src_mac), payload.temperature);
}

bool has_received_reading() {
    if (reading_available) {
        reading_available = false;
        return true;
    }
    return false;
}

float get_received_temperature() {
    return received_temperature;
}

MacAddr get_received_sender() {
    return received_sender;
}

void send_reading(MacAddr dest_mac, float temperature) {
    ReadingPayload payload{.temperature = temperature};

    std::array<uint8_t, sizeof(ReadingPayload)> data{};
    memcpy(data.data(), &payload, sizeof(payload));

    driver::network::esp_now::send_msg(RxCommand::READING, dest_mac, data);
}

} // namespace service::network
