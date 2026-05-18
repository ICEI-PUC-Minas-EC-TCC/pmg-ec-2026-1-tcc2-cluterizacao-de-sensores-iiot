#include "AmmeterService/ammeter_service.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cmath>

namespace service::ammeter {

static const char *TAG = "AMMETER_SERVICE";

// ================= CONFIG =================
static constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;

// Canal da corrente (amplificador)
static constexpr adc_channel_t ADC_CHANNEL_CURRENT = ADC_CHANNEL_0;

// Canal da tensão da bateria
static constexpr adc_channel_t ADC_CHANNEL_VBAT = ADC_CHANNEL_3;

static constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;

static constexpr uint32_t SAMPLE_INTERVAL_MS = 200;
static constexpr int AVG_SAMPLES = 32;

// Hardware
static constexpr float SHUNT_OHMS = 0.1f;
static constexpr float AMPLIFIER_GAIN = 11.0f;

// Divisor de tensão da bateria (ex: 100k / 100k)
static constexpr float VBAT_DIVIDER_RATIO = 2.0f;

// Capacidade
static constexpr float BATTERY_CAPACITY_MAH = 1000.0f;

// Filtro
static constexpr float EMA_ALPHA = 0.2f;

// ================= ESTADO =================

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_cali_handle_t adc_cali_handle = nullptr;
static bool adc_cali_enabled = false;

static bool initialized = false;
static bool new_measurement_available = false;

static Measurement last_measurement{};

static int64_t last_sample_time_us = 0;

// offset do sensor (auto calibrado)
static float current_offset_a = 0.0f;
static bool offset_initialized = false;

// ================= UTIL =================

static int adc_raw_to_mv(int raw) {
    int voltage_mv = 0;

    if (adc_cali_enabled &&
        adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage_mv) == ESP_OK) {
        return voltage_mv;
    }

    // fallback simples
    return (raw * 1100) / 4095;
}

static int read_adc_avg(adc_channel_t channel) {
    int64_t sum = 0;

    for (int i = 0; i < AVG_SAMPLES; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, channel, &raw) == ESP_OK) {
            sum += raw;
        }
    }

    return sum / std::max(AVG_SAMPLES, 1);
}

// ================= INIT =================

void init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_CURRENT,
                                               &channel_cfg));
    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_VBAT, &channel_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle) ==
        ESP_OK) {
        adc_cali_enabled = true;
    }
#endif

    last_measurement = {};
    last_measurement.remaining_mah = BATTERY_CAPACITY_MAH;
    last_measurement.battery_pct = 100.0f;

    last_sample_time_us = esp_timer_get_time();
    initialized = true;

    ESP_LOGI(TAG, "Initialized");
}

// ================= HANDLER =================

void handler() {
    if (!initialized)
        return;

    const int64_t now = esp_timer_get_time();
    const int64_t elapsed_us = now - last_sample_time_us;

    if (elapsed_us < SAMPLE_INTERVAL_MS * 1000)
        return;

    // ================= LEITURA =================

    int raw_current = read_adc_avg(ADC_CHANNEL_CURRENT);
    int raw_vbat = read_adc_avg(ADC_CHANNEL_VBAT);

    float v_amp = adc_raw_to_mv(raw_current) / 1000.0f;
    float vbat = (adc_raw_to_mv(raw_vbat) / 1000.0f) * VBAT_DIVIDER_RATIO;

    float v_shunt = v_amp / AMPLIFIER_GAIN;
    float current_a = v_shunt / SHUNT_OHMS;

    // ================= OFFSET =================

    if (!offset_initialized) {
        current_offset_a = current_a;
        offset_initialized = true;
    }

    current_a -= current_offset_a;

    // zona morta
    if (std::fabs(current_a) < 0.002f) {
        current_a = 0.0f;
    }

    // ================= FILTRO =================

    static float filtered_current = 0.0f;
    filtered_current =
        (EMA_ALPHA * current_a) + (1.0f - EMA_ALPHA) * filtered_current;

    // ================= POTÊNCIA =================

    float power_w = filtered_current * vbat;

    // ================= INTEGRAÇÃO =================

    float elapsed_h = elapsed_us / 3600000000.0f;

    float delta_mah = filtered_current * 1000.0f * elapsed_h;
    float delta_mwh = power_w * 1000.0f * elapsed_h;

    last_measurement.current_ma = filtered_current * 1000.0f;
    last_measurement.power_mw = power_w * 1000.0f;

    last_measurement.consumed_mah += delta_mah;
    last_measurement.consumed_mwh += delta_mwh;

    last_measurement.remaining_mah =
        std::max(BATTERY_CAPACITY_MAH - last_measurement.consumed_mah, 0.0f);

    last_measurement.battery_pct =
        (last_measurement.remaining_mah / BATTERY_CAPACITY_MAH) * 100.0f;

    last_measurement.adc_raw = raw_current;
    last_measurement.adc_mv = adc_raw_to_mv(raw_current);

    last_sample_time_us = now;
    new_measurement_available = true;

    ESP_LOGI(TAG, "I=%.2fmA V=%.2fV P=%.2fmW Rem=%.1f%%",
             last_measurement.current_ma, vbat, last_measurement.power_mw,
             last_measurement.battery_pct);
}

// ================= GETTERS =================

Measurement get_last_measurement() {
    return last_measurement;
}

bool has_new_measurement() {
    if (!new_measurement_available)
        return false;
    new_measurement_available = false;
    return true;
}

} // namespace service::ammeter