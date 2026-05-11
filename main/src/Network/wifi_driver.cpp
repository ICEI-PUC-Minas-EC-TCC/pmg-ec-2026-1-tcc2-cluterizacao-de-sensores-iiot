#include "Network/wifi_driver.hpp"
#include "MqttService/mqtt_controller.hpp"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <string.h>

static const char* TAG = "WIFI_DRIVER";

// ESP-NOW shares the radio with Wi-Fi and uses whatever channel the STA is
// locked to. Without an active AP association the channel is undefined and
// peers may end up on different channels, losing 100% of frames. We pin the
// radio to the AP's channel at startup so ESP-NOW works regardless of which
// node currently holds the leader role (and is associated to the AP).
static constexpr uint8_t ESP_NOW_CHANNEL = 1;

// Tracks the desired association state. Auto-reconnect on STA_DISCONNECTED
// only fires when this is true; otherwise the disconnect was intentional
// (e.g. we became MEMBER and don't want the AP association).
static bool should_be_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Interface Wi-Fi iniciada.");

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*) event_data;
        controller::mqtt::set_wifi_status(false);

        if (should_be_connected) {
            ESP_LOGW(TAG, "Conexao Wi-Fi perdida (reason=%u). Tentando reconexao...", d->reason);
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "Wi-Fi desassociado (esperado, reason=%u).", d->reason);
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Conexao estabelecida. IP alocado: " IPSTR, IP2STR(&event->ip_info.ip));
        controller::mqtt::set_wifi_status(true);
    }
}

void driver::wifi::init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable    = true;
    wifi_config.sta.pmf_cfg.required   = false;
    wifi_config.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
}

void driver::wifi::connect() {
    if (should_be_connected) {
        return;
    }
    should_be_connected = true;
    ESP_LOGI(TAG, "Conectando ao AP...");
    esp_wifi_connect();
}

void driver::wifi::disconnect() {
    if (!should_be_connected) {
        return;
    }
    should_be_connected = false;
    ESP_LOGI(TAG, "Desassociando do AP...");
    esp_wifi_disconnect();
}
