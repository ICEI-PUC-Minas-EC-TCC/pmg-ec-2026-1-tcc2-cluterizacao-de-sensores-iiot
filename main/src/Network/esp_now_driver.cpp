#include "Network/esp_now_driver.hpp"
#include "LedService/led_controller.hpp"
#include "Network/network_controller.hpp"
#include "Network/network_service.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi_types_generic.h"
#include "espnow.h"
#include "freertos/idf_additions.h"
#include "utils.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <esp_now.h>

namespace driver::network::esp_now {

using controller::network::MacAddr;
using service::network::RxCommand;

static bool isInitialized = 0;

static constexpr uint8_t MAX_CALLBACK = UINT8_MAX;

static constexpr uint8_t ARRIVED_QUEUE_LENGTH = 10;
static constexpr uint8_t DEPARTURE_QUEUE_LENGTH = 10;

static MacAddr MY_MAC;

std::array<void (*)(Packet), MAX_CALLBACK> RX_CALLBACK_TABLE;

static QueueHandle_t esp_now_arrived_queue;
static QueueHandle_t esp_now_departure_queue;

void handler_departure();
void handler_arrived();

void register_rx_callback(void (*func)(Packet), RxCommand cmd) {
    CHECK_INITIALIZED();

    if (cmd >= RxCommand::SIZE) {
        ESP_LOGE(__FUNCTION__, "cmd: %u is not a valid RxCommand", cmd);
        return;
    }
    RX_CALLBACK_TABLE[static_cast<uint8_t>(cmd)] = func;
}

void rx_callback(const esp_now_recv_info_t *info, const uint8_t *data, int size) {
    ESP_LOGI(__FUNCTION__, "Received from " MACSTR " | size=%d",
             MAC2STR(info->src_addr), size);

    if (size != sizeof(Packet)) {
        ESP_LOGW(__FUNCTION__, "Unexpected size: %d (expected %d)", size, (int)sizeof(Packet));
    }

    Packet packet;
    memset(&packet, 0, sizeof(packet));
    memcpy(&packet, data, size);

    if (xQueueSend(esp_now_arrived_queue, &packet, 0) != pdTRUE) {
        ESP_LOGW(__FUNCTION__, "Arrived queue full, packet dropped");
    }
}

void send_callback(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    if (info->des_addr == nullptr) {
        return;
    }
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(__FUNCTION__, "Sent to " MACSTR " OK", MAC2STR(info->des_addr));
    } else {
        ESP_LOGW(__FUNCTION__, "Send failed to " MACSTR, MAC2STR(info->des_addr));
    }
}

void place_packet_departure_queue(Packet packet) {
    xQueueSend(esp_now_departure_queue, &packet, 0);
}

void init() {
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(rx_callback));
    ESP_ERROR_CHECK(esp_now_register_send_cb(send_callback));

    esp_now_arrived_queue   = xQueueCreate(ARRIVED_QUEUE_LENGTH,   sizeof(Packet));
    esp_now_departure_queue = xQueueCreate(DEPARTURE_QUEUE_LENGTH, sizeof(Packet));

    add_peer(ESPNOW_ADDR_BROADCAST, 0);

    esp_wifi_get_mac(WIFI_IF_STA, MY_MAC.data());

    isInitialized = true;
}

void handler() {
    CHECK_INITIALIZED();
    handler_arrived();
    handler_departure();
}

esp_err_t add_peer(const uint8_t *mac, uint8_t channel) {
    if (esp_now_is_peer_exist(mac)) {
        return ESP_ERR_ESPNOW_EXIST;
    }

    esp_now_peer_info_t peer = {};
    peer.channel = channel;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt  = false;
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);

    return esp_now_add_peer(&peer);
}

Packet packer(RxCommand cmd, MacAddr dest_mac, std::span<const uint8_t> data) {
    static Packet packet;

    memset(&packet, 0, sizeof(packet));
    packet.command = static_cast<uint8_t>(cmd);
    memcpy(packet.src_mac,  MY_MAC.data(),   sizeof(MY_MAC));
    memcpy(packet.dest_mac, &dest_mac,       sizeof(dest_mac));
    memcpy(packet.data,     data.data(),     data.size());

    return packet;
}

void send_msg(RxCommand cmd, MacAddr dest_addr, std::span<const uint8_t> data) {
    Packet packet = packer(cmd, dest_addr, data);

    if (memcmp(packet.dest_mac, MY_MAC.data(), sizeof(MY_MAC)) == 0) {
        return;
    }

    place_packet_departure_queue(packet);
}

void send_broadcast(RxCommand cmd, std::span<const uint8_t> data) {
    MacAddr broadcast_addr = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    Packet packet = packer(cmd, broadcast_addr, data);
    place_packet_departure_queue(packet);
}

void handler_departure() {
    static Packet  packet;
    static uint8_t buffer[sizeof(packet)];

    memset(&packet, 0, sizeof(packet));

    if (xQueueReceive(esp_now_departure_queue, &packet, 0) == pdFALSE) {
        return;
    }

    memcpy(buffer, &packet, sizeof(buffer));

    ESP_LOGI(__FUNCTION__, "Sending to " MACSTR " | cmd=%u",
             MAC2STR(packet.dest_mac), packet.command);

    esp_err_t ret = esp_now_send(packet.dest_mac, buffer, sizeof(buffer));
    if (ret == ESP_ERR_ESPNOW_NOT_FOUND) {
        ESP_LOGW(__FUNCTION__, "Peer not found: " MACSTR, MAC2STR(packet.dest_mac));
    } else if (ret != ESP_OK) {
        ESP_LOGW(__FUNCTION__, "Send error: %s", esp_err_to_name(ret));
    }
}

void handler_arrived() {
    static Packet packet;

    memset(&packet, 0, sizeof(packet));

    if (xQueueReceive(esp_now_arrived_queue, &packet, 0) == pdFALSE) {
        return;
    }

    RxCommand cmd = static_cast<RxCommand>(packet.command);
    if (cmd >= RxCommand::SIZE) {
        ESP_LOGW(__FUNCTION__, "cmd: %u invalid | src: " MACSTR,
                 cmd, MAC2STR(packet.src_mac));
        return;
    }

    if (RX_CALLBACK_TABLE[static_cast<uint8_t>(cmd)] == 0) {
        ESP_LOGW(__FUNCTION__, "cmd: %u not registered | src: " MACSTR,
                 cmd, MAC2STR(packet.src_mac));
        return;
    }

    RX_CALLBACK_TABLE[static_cast<uint8_t>(cmd)](packet);
}

} // namespace driver::network::esp_now
