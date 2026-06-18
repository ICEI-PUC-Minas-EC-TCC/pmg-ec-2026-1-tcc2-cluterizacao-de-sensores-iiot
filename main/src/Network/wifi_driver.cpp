#include "Network/wifi_driver.hpp"
#include "LedService/led_controller.hpp"
#include "MqttService/mqtt_controller.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <string.h>

static const char *TAG = "WIFI_DRIVER";

// Workaround p/ defeito de RF do ESP32-C3 SuperMini: potencia alta na antena
// mal casada distorce o TX e quebra o handshake. Unidade = 0.25 dBm.
// 34 = 8.5 dBm (valor reportado como estavel). Aplicado apos esp_wifi_start().
static constexpr int8_t MAX_TX_POWER = 34;

// SoftAP placeholder: nao serve clientes uteis. Existe so para manter o radio
// em duty cycle de RX 100% (no canal fixo) enquanto a associacao STA do membro
// esta desligada, garantindo que o ESP-NOW continue recebendo.
static constexpr char AP_SSID[] = "tcc-node";

// Exponential backoff schedule (ms) for STA reconnect attempts.
// Reset to index 0 on successful association.
static const uint32_t BACKOFF_MS[] = {500, 1000, 2000, 3000, 5000};
static constexpr size_t BACKOFF_LEN =
    sizeof(BACKOFF_MS) / sizeof(BACKOFF_MS[0]);

static size_t backoff_idx = 0;
static TimerHandle_t reconnect_timer = nullptr;

// true somente quando o no deve manter a associacao STA (papel LEADER).
// Garante que desconexoes intencionais (membro) nao disparem reconexao e
// torna connect()/disconnect() idempotentes.
static bool should_be_connected = false;

static void reconnect_timer_cb(TimerHandle_t) {
    if (should_be_connected) {
        esp_wifi_connect();
    }
}

static void schedule_reconnect() {
    if (reconnect_timer == nullptr) {
        return;
    }
    uint32_t delay_ms = BACKOFF_MS[backoff_idx];
    if (backoff_idx + 1 < BACKOFF_LEN) {
        backoff_idx++;
    }
    ESP_LOGI(TAG, "Proxima tentativa em %u ms (idx=%u)", (unsigned)delay_ms,
             (unsigned)backoff_idx);
    xTimerStop(reconnect_timer, 0);
    xTimerChangePeriod(reconnect_timer, pdMS_TO_TICKS(delay_ms), 0);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        // Nao associa automaticamente: quem governa a associacao STA agora e
        // o role_service (connect() ao virar LEADER).
        ESP_LOGI(TAG,
                 "Interface STA iniciada (aguardando papel para associar).");

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d =
            (wifi_event_sta_disconnected_t *)event_data;
        controller::mqtt::set_wifi_status(false);

        if (should_be_connected) {
            ESP_LOGW(TAG,
                     "Conexao Wi-Fi perdida (reason=%u, rssi=%d). Tentando "
                     "reconexao...",
                     d->reason, d->rssi);
            schedule_reconnect();
        } else {
            ESP_LOGI(TAG, "STA desassociado (papel MEMBER) — sem reconexao.");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            ESP_LOGI(TAG, "Conexao estabelecida. IP=" IPSTR " rssi=%d ch=%u",
                     IP2STR(&event->ip_info.ip), ap.rssi, ap.primary);
        } else {
            ESP_LOGI(TAG, "Conexao estabelecida. IP alocado: " IPSTR,
                     IP2STR(&event->ip_info.ip));
        }
        backoff_idx = 0;
        controller::mqtt::set_wifi_status(true);
    }
}

void driver::wifi::init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &instance_any_id));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
        &instance_got_ip));

    reconnect_timer = xTimerCreate("wifi_reconnect", pdMS_TO_TICKS(1000),
                                   pdFALSE, nullptr, reconnect_timer_cb);

    // --- STA: cliente do roteador (so associa quando LEADER) ---
    wifi_config_t sta_config = {};
    strcpy((char *)sta_config.sta.ssid, CONFIG_WIFI_SSID);
    strcpy((char *)sta_config.sta.password, CONFIG_WIFI_PASSWORD);
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_config.sta.pmf_cfg.capable = true;
    sta_config.sta.pmf_cfg.required = false;
    sta_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    // Procura o AP apenas no canal fixo: associacao mais rapida e mantem a
    // coerencia de canal do APSTA.
    sta_config.sta.channel = CONFIG_NETWORK_FIXED_CHANNEL;

    // --- AP placeholder: mantem o radio acordado para o ESP-NOW ---
    wifi_config_t ap_config = {};
    strcpy((char *)ap_config.ap.ssid, AP_SSID);
    ap_config.ap.ssid_len = strlen(AP_SSID);
    ap_config.ap.channel = CONFIG_NETWORK_FIXED_CHANNEL;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.ssid_hidden = 1; // nao polui as redes proximas
    ap_config.ap.max_connection = 1;
    ap_config.ap.beacon_interval = 100;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Workaround de RF: deve ser chamado APOS esp_wifi_start().
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(MAX_TX_POWER));

    ESP_LOGI(TAG,
             "Wi-Fi APSTA iniciado (SoftAP placeholder ch=%d). STA aguardando "
             "papel.",
             CONFIG_NETWORK_FIXED_CHANNEL);
}

void driver::wifi::connect() {
    if (should_be_connected) {
        return; // ja associado/associando
    }
    should_be_connected = true;
    backoff_idx = 0;
    ESP_LOGI(TAG, "Papel LEADER: associando ao AP...");
    esp_wifi_connect();
}

void driver::wifi::disconnect() {
    if (!should_be_connected) {
        return; // ja desassociado (ou nunca associou)
    }
    should_be_connected = false;
    if (reconnect_timer != nullptr) {
        xTimerStop(reconnect_timer, 0);
    }
    ESP_LOGI(TAG, "Papel MEMBER: desassociando STA (SoftAP/ESP-NOW seguem "
                  "ativos).");
    esp_wifi_disconnect();
    controller::mqtt::set_wifi_status(false);
}
