#include "Network/wifi_driver.hpp"
#include "MqttService/mqtt_controller.hpp"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>

static const char* TAG = "WIFI_DRIVER";

// Exponential backoff schedule (ms) for STA reconnect attempts.
// Reset to index 0 on successful association.
static const uint32_t BACKOFF_MS[] = {1000, 2000, 5000, 10000, 30000};
static constexpr size_t BACKOFF_LEN = sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]);

static size_t        backoff_idx     = 0;
static TimerHandle_t reconnect_timer = nullptr;

static void reconnect_timer_cb(TimerHandle_t) {
    esp_wifi_connect();
}

static void schedule_reconnect() {
    if (reconnect_timer == nullptr) {
        return;
    }
    uint32_t delay_ms = BACKOFF_MS[backoff_idx];
    if (backoff_idx + 1 < BACKOFF_LEN) {
        backoff_idx++;
    }
    ESP_LOGI(TAG, "Proxima tentativa em %u ms (idx=%u)",
             (unsigned)delay_ms, (unsigned)backoff_idx);
    xTimerStop(reconnect_timer, 0);
    xTimerChangePeriod(reconnect_timer, pdMS_TO_TICKS(delay_ms), 0);
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Interface Wi-Fi iniciada. Conectando ao AP...");
        backoff_idx = 0;
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* d = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Conexao Wi-Fi perdida (reason=%u, rssi=%d). Tentando reconexao...",
                 d->reason, d->rssi);
        controller::mqtt::set_wifi_status(false);
        schedule_reconnect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Conexao estabelecida. IP=" IPSTR " rssi=%d ch=%u",
                     IP2STR(&event->ip_info.ip), ap.rssi, ap.primary);
        } else {
            ESP_LOGI(TAG, "Conexao estabelecida. IP alocado: " IPSTR, IP2STR(&event->ip_info.ip));
        }
        backoff_idx = 0;
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

    reconnect_timer = xTimerCreate("wifi_reconnect",
                                   pdMS_TO_TICKS(1000),
                                   pdFALSE,
                                   nullptr,
                                   reconnect_timer_cb);

    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, CONFIG_WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, CONFIG_WIFI_PASSWORD);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable    = true;
    wifi_config.sta.pmf_cfg.required   = false;
    wifi_config.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;

    // Stop at the first matching SSID instead of full-channel sweeps,
    // so reconnect attempts spend less time channel-hopping.
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());
}
