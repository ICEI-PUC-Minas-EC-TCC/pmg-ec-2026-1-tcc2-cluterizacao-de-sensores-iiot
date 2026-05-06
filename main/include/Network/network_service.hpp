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

    SIZE,
};

enum class TxCommand : uint8_t {
    ACK = 0,
    PING,
    READING,

    SIZE,
};

// Payload do comando READING enviado via ESP-NOW do membro ao líder
struct __attribute__((packed)) ReadingPayload {
    float temperature;
};

void init();
void handler();

void ping_broadcast();
void ping_peer(controller::network::MacAddr dest_mac);
esp_err_t add_esp_peer(controller::network::MacAddr peer_mac, uint8_t peer_channel);

void send_reading(controller::network::MacAddr dest_mac, float temperature);

const std::vector<controller::network::MacAddr>& get_known_peers();

// Leitura recebida de um membro — consumida pelo líder no application_controller
bool has_received_reading();
float get_received_temperature();
controller::network::MacAddr get_received_sender();

} // namespace service::network