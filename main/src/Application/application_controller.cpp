#include "LedService/led_controller.hpp"
#include "Network/network_controller.hpp"
#include "Application/application_controller.hpp"
#include "Application/button_service.hpp"
#include "Application/discover_service.hpp"
#include "Application/reading_service.hpp"
#include "Application/role_service.hpp"
#include "Network/network_service.hpp"
#include "MqttService/mqtt_controller.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"

#include <cstdio>

static const char *TAG = "APP_CONTROLLER";

static void handle_leader();
static void handle_member();

void controller::application::init() {
    controller::led::init();
    controller::network::init();
    controller::mqtt::init();

    service::application::button::init();
    service::application::discover::init();
    service::application::role::init();
    service::application::reading::init();

    xTaskCreate(controller::application::handler,
                "application_controller_handler", 4096, NULL, 5, NULL);
}

void controller::application::handler(void *arg) {
    for (;;) {
        service::application::button::handler();
        service::application::discover::handler();
        service::application::role::handler();
        service::application::reading::handler();

        switch (service::application::role::get_role()) {
        case service::application::role::Role::LEADER:
            handle_leader();
            break;
        case service::application::role::Role::MEMBER:
            handle_member();
            break;
        default:
            break;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

static void handle_leader() {
    char topic[64];
    char payload[64];

    uint8_t own_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, own_mac);

    if (service::application::reading::has_new_reading()) {
        float temp = service::application::reading::get_last_reading();

        snprintf(topic,   sizeof(topic),   "/tcc/%02x%02x%02x%02x%02x%02x",
                 own_mac[0], own_mac[1], own_mac[2],
                 own_mac[3], own_mac[4], own_mac[5]);
        snprintf(payload, sizeof(payload), "{\"temperature\": %.1f}", temp);

        controller::mqtt::publish(topic, payload);
        ESP_LOGI(TAG, "[LEADER] Published: %s -> %s", topic, payload);
    }

    if (service::network::has_received_reading()) {
        float temp   = service::network::get_received_temperature();
        auto  sender = service::network::get_received_sender();

        snprintf(topic,   sizeof(topic),   "/tcc/%02x%02x%02x%02x%02x%02x",
                 sender[0], sender[1], sender[2],
                 sender[3], sender[4], sender[5]);
        snprintf(payload, sizeof(payload), "{\"temperature\": %.1f}", temp);

        controller::mqtt::publish(topic, payload);
        ESP_LOGI(TAG, "[LEADER] Member reading: %s -> %s", topic, payload);
    }
}

static void handle_member() {
    if (!service::application::reading::has_new_reading()) {
        return;
    }

    float temp   = service::application::reading::get_last_reading();
    auto  leader = service::application::role::get_leader_mac();

    service::network::send_reading(leader, temp);
    ESP_LOGI(TAG, "[MEMBER] Sent reading %.1f C to leader", temp);
}
