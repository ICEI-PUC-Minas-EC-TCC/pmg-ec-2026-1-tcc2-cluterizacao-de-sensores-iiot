#include "Network/network_controller.hpp"

#include "Network/esp_now_driver.hpp"
#include "Network/network_service.hpp"
#include "Network/wifi_driver.hpp"

#include "dhcpserver/dhcpserver_options.h"
#include "freertos/idf_additions.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace controller::network {

enum cmd_type {
    PING_ALL,
    PING_PEER,
    ADD_PEAR,
};

typedef union {
    struct {
    } PING_ALL;

    struct {
        MacAddr dest_mac;
    } PING_PEER;

    struct {
        MacAddr peer_mac;
        uint8_t peer_channel;
    } ADD_PEER;

} network_cmd_data_t;

typedef struct {
    cmd_type type;
    network_cmd_data_t data;
} network_cmd_t;

static QueueHandle_t network_queue;

void init() {
    driver::wifi::init();
    driver::network::esp_now::init();

    service::network::init();

    network_queue = xQueueCreate(10, sizeof(network_cmd_t));

    xTaskCreate(handler, "network_controller", 4096, NULL, 5, NULL);
}

void handler(void *arg) {
    network_cmd_t cmd;

    for (;;) {
        if (xQueueReceive(network_queue, &cmd, pdMS_TO_TICKS(50))) {
            switch (cmd.type) {
            case PING_ALL:
                service::network::ping_broadcast();
                break;
            case PING_PEER:
                service::network::ping_peer(cmd.data.PING_PEER.dest_mac);
                break;
            case ADD_PEAR:
                service::network::add_esp_peer(cmd.data.ADD_PEER.peer_mac,
                                               cmd.data.ADD_PEER.peer_channel);
                break;
            }
        }

        service::network::handler();
    }
}

void send_ping_device(MacAddr addr) {
    network_cmd_data_t data;

    memcpy(data.PING_PEER.dest_mac.data(), addr.data(), sizeof(addr));

    network_cmd_t cmd = {.type = PING_PEER, .data = data};

    xQueueSend(network_queue, &cmd, portMAX_DELAY);
};

void send_ping_broadcast() {
    network_cmd_t cmd = {.type = PING_ALL, .data = {.PING_ALL = {}}};

    xQueueSend(network_queue, &cmd, portMAX_DELAY);
};

void add_pear() {
    network_cmd_t cmd = {.type = ADD_PEAR, .data = {.PING_ALL = {}}};

    xQueueSend(network_queue, &cmd, portMAX_DELAY);
}
} // namespace controller::network
