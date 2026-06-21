#pragma once

#include "Network/network_controller.hpp"
#include "Network/network_service.hpp"

#include "esp_err.h"
#include "esp_wifi.h"

#include <cstdint>
#include <span>

namespace driver::network::esp_now {

using controller::network::MacAddr;
using service::network::RxCommand;

constexpr uint8_t MAX_SIZE_PACKET = 200;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint8_t retrys;
} PacketHeader;

typedef struct __attribute__((packed)) {
    PacketHeader header;
    uint8_t command;
    uint8_t src_mac[6];
    uint8_t dest_mac[6];
    uint8_t data[MAX_SIZE_PACKET - sizeof(PacketHeader)];
} Packet;

void init();

void handler();

void register_rx_callback(void (*func)(Packet),
                          service::network::RxCommand cmd);

void send_msg(RxCommand cmd, MacAddr dest_addr, std::span<const uint8_t> data);

void send_broadcast(RxCommand cmd, std::span<const uint8_t> data);

esp_err_t add_peer(const uint8_t *mac, uint8_t channel);

} // namespace driver::network::esp_now