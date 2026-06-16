#include "RtcService/rtc_service.hpp"

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "sdkconfig.h"

#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/time.h>

static const char *TAG = "RTC_SERVICE";

static volatile bool s_synced = false;

// Funcao pura de formatacao, isolada para clareza/testabilidade.
// Converte um time_t (epoch) em "dd/mm/aaaa hh:mm:ss" no fuso ja
// configurado em TZ via tzset().
static std::string format_brt(time_t t) {
    struct tm tm_local;
    localtime_r(&t, &tm_local);

    char buf[20]; // "dd/mm/aaaa hh:mm:ss" = 19 chars + '\0'
    strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M:%S", &tm_local);
    return std::string(buf);
}

// Callback chamado pelo daemon SNTP a cada sincronizacao (inclusive resync).
static void on_sync(struct timeval *tv) {
    s_synced = true;
    time_t now = tv ? tv->tv_sec : time(nullptr);
    ESP_LOGI(TAG, "Horario sincronizado via SNTP: %s", format_brt(now).c_str());
}

void service::rtc::init() {
    // Fuso de Brasilia (UTC-3, sem horario de verao) — POSIX TZ.
    setenv("TZ", CONFIG_RTC_TIMEZONE, 1);
    tzset();

    esp_sntp_config_t config =
        ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_RTC_NTP_SERVER);
    config.sync_cb = on_sync;

    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    ESP_LOGI(TAG, "Cliente SNTP iniciado (servidor=%s, TZ=%s)",
             CONFIG_RTC_NTP_SERVER, CONFIG_RTC_TIMEZONE);
}

std::string service::rtc::get_current_time() {
    return format_brt(time(nullptr));
}

bool service::rtc::is_synced() {
    return s_synced;
}
