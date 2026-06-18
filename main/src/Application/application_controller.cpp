#include "Application/application_controller.hpp"
#include "AmmeterService/ammeter_service.hpp"
#include "Application/button_service.hpp"
#include "Application/discover_service.hpp"
#include "Application/energy_service.hpp"
#include "Application/reading_service.hpp"
#include "Application/role_service.hpp"
#include "LedService/led_controller.hpp"
#include "MqttService/mqtt_controller.hpp"
#include "Network/network_controller.hpp"
#include "Network/network_service.hpp"
#include "RtcService/rtc_service.hpp"
#include "TaskPriorities.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>
#include <cstdio>

static const char *TAG = "APP_CONTROLLER";

static void handle_leader();
static void handle_member();

static const char *role_label(service::application::role::Role r) {
    using service::application::role::Role;
    switch (r) {
    case Role::LEADER:
        return "LEADER";
    case Role::MEMBER:
        return "MEMBER";
    default:
        return "IDLE";
    }
}

void controller::application::init() {
    service::ammeter::init();
    service::rtc::init();
    service::application::button::init();
    service::application::discover::init();
    service::application::energy::init();
    service::application::role::init();
    service::application::reading::init();

    controller::led::init();
    controller::network::init();
    controller::mqtt::init();

    xTaskCreate(
        controller::application::handler, "application_controller_handler",
        4096, NULL,
        static_cast<uint8_t>(task_priorities::TaskPrioritie::application),
        NULL);
}

void controller::application::handler(void *arg) {
    uint32_t calib_ticks = 0;
    for (;;) {
        service::application::button::handler();
        service::application::discover::handler();
        service::application::energy::tick();
        service::application::role::handler();
        service::application::reading::handler();
        if (service::network::has_received_rotate()) {
            service::application::role::on_rotate_received(
                service::network::get_rotate_next_leader());
        }

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

        // Linha unica de calibracao (~1s): casa a corrente do INA219 com o papel
        // atual, inclusive no ocioso (UNDECIDED->IDLE), que as linhas
        // [LEADER]/[MEMBER] nao cobrem. Fonte preferida do parser
        // analysis/calibration.py.
        if (++calib_ticks >= 10) {
            calib_ticks = 0;
            auto m = service::ammeter::get_last_measurement();
            ESP_LOGI("CALIB", "role=%s I=%.2fmA bat=%.1f%%",
                     role_label(service::application::role::get_role()),
                     m.current_ma, m.battery_pct);
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void handle_leader() {
    char topic[64];
    char payload[256];

    uint8_t own_mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, own_mac);

    bool has_new_temperature = service::application::reading::has_new_reading();

    if (has_new_temperature) {
        float temp = service::application::reading::get_last_reading();
        auto m = service::ammeter::get_last_measurement();

        snprintf(topic, sizeof(topic), "/tcc/main/%02x%02x%02x%02x%02x%02x",
                 own_mac[0], own_mac[1], own_mac[2], own_mac[3], own_mac[4],
                 own_mac[5]);
        snprintf(payload, sizeof(payload),
                 "{\"temperature\": %.1f, \"current_ma\": %.1f, "
                 "\"battery_pct\": %.1f, \"measured_time\": \"%s\"}",
                 temp, m.current_ma, m.battery_pct,
                 service::rtc::get_current_time().c_str());

        controller::mqtt::publish(topic, payload);
        service::application::energy::on_mqtt_publish();
        ESP_LOGI(TAG, "[LEADER] Published: %s -> %s", topic, payload);
    }

    if (service::network::has_received_reading()) {
        float temp = service::network::get_received_temperature();
        float current_ma = service::network::get_received_current_ma();
        float battery_pct = service::network::get_received_battery_pct();
        auto sender = service::network::get_received_sender();

        snprintf(topic, sizeof(topic), "/tcc/main/%02x%02x%02x%02x%02x%02x",
                 sender[0], sender[1], sender[2], sender[3], sender[4],
                 sender[5]);
        snprintf(payload, sizeof(payload),
                 "{\"temperature\": %.1f, \"current_ma\": %.1f, "
                 "\"battery_pct\": %.1f, \"measured_time\": \"%s\"}",
                 temp, current_ma, battery_pct,
                 service::rtc::get_current_time().c_str());

        controller::mqtt::publish(topic, payload);
        service::application::energy::on_mqtt_publish();
        ESP_LOGI(TAG, "[LEADER] Member reading: %s -> %s", topic, payload);
    }
}

static void handle_member() {
    if (!service::application::reading::has_new_reading()) {
        return;
    }

    float temp = service::application::reading::get_last_reading();
    auto leader = service::application::role::get_leader_mac();
    auto m = service::ammeter::get_last_measurement();

    service::network::send_reading(leader, temp, m.current_ma, m.battery_pct);
    service::application::energy::on_espnow_send();
    ESP_LOGI(TAG, "[MEMBER] Sent reading %.1f C, %.1f mA, %.1f%% to leader",
             temp, m.current_ma, m.battery_pct);
}
