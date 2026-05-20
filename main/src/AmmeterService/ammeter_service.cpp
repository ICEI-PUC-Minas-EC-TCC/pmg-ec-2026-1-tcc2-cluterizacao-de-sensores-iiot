#include "AmmeterService/ammeter_service.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <algorithm>
#include <cmath>

namespace service::ammeter {

static const char *TAG = "AMMETER_SERVICE";

// ================= CONFIG (via Kconfig) =================
static constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;

// Canal da corrente (amplificador) -> GPIO0 no ESP32-C3
static constexpr adc_channel_t ADC_CHANNEL_CURRENT = ADC_CHANNEL_0;

// Canal da tensão da bateria -> GPIO3 no ESP32-C3
static constexpr adc_channel_t ADC_CHANNEL_VBAT = ADC_CHANNEL_3;

static constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;

static constexpr uint32_t SAMPLE_INTERVAL_MS = CONFIG_AMMETER_SAMPLE_INTERVAL_MS;
static constexpr int AVG_SAMPLES = CONFIG_AMMETER_ADC_AVG_SAMPLES;

// Hardware (Kconfig)
static constexpr float SHUNT_OHMS = CONFIG_AMMETER_SHUNT_OHMS_X1000 / 1000.0f;
static constexpr float AMPLIFIER_GAIN = CONFIG_AMMETER_AMPLIFIER_GAIN_X100 / 100.0f;

// Divisor de tensão da bateria (ex: 100k / 100k)
static constexpr float VBAT_DIVIDER_RATIO = 2.0f;

// Tensao nominal usada quando o divisor de VBAT nao esta montado
static constexpr float NOMINAL_VBAT_V = CONFIG_AMMETER_BATTERY_VOLTAGE_MV / 1000.0f;

// Capacidade (Kconfig)
static constexpr float BATTERY_CAPACITY_MAH = CONFIG_AMMETER_BATTERY_CAPACITY_MAH;

// Vref fallback do ADC (quando a calibracao curve-fitting nao esta disponivel)
static constexpr int ADC_DEFAULT_VREF_MV = CONFIG_AMMETER_ADC_DEFAULT_VREF_MV;

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

#if CONFIG_AMMETER_AUTO_ZERO_ON_BOOT
// offset do sensor (auto calibrado na 1a leitura - opt-in)
static float current_offset_a = 0.0f;
static bool offset_initialized = false;
#endif

// ================= UTIL =================

static int adc_raw_to_mv(int raw) {
    int voltage_mv = 0;

    if (adc_cali_enabled &&
        adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage_mv) == ESP_OK) {
        return voltage_mv;
    }

    // fallback simples
    return (raw * ADC_DEFAULT_VREF_MV) / 4095;
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
    int adc_mv_current = adc_raw_to_mv(raw_current);

#if CONFIG_AMMETER_USE_VBAT_DIVIDER
    int raw_vbat = read_adc_avg(ADC_CHANNEL_VBAT);
    float vbat = (adc_raw_to_mv(raw_vbat) / 1000.0f) * VBAT_DIVIDER_RATIO;
#else
    float vbat = NOMINAL_VBAT_V;
#endif

    // Offset fixo do AMPOP, calibrado em bancada com I=0.
    // Subtraido em mV no dominio da saida do amplificador, antes do
    // ganho/shunt, para evitar baseline dependente do consumo no boot.
    float v_amp = (adc_mv_current - CONFIG_AMMETER_OFFSET_MV) / 1000.0f;

    float v_shunt = v_amp / AMPLIFIER_GAIN;
    float current_a = v_shunt / SHUNT_OHMS;

    // ================= OFFSET (auto-zero opt-in / legacy) =================

#if CONFIG_AMMETER_AUTO_ZERO_ON_BOOT
    if (!offset_initialized) {
        current_offset_a = current_a;
        offset_initialized = true;
        ESP_LOGW(TAG, "Auto-zero capturado no boot: offset=%.3fmA",
                 current_offset_a * 1000.0f);
    }
    current_a -= current_offset_a;
#endif

    // Zona morta configuravel (uA -> A)
    constexpr float DEAD_ZONE_A = CONFIG_AMMETER_DEAD_ZONE_UA / 1000000.0f;
    if (DEAD_ZONE_A > 0.0f && std::fabs(current_a) < DEAD_ZONE_A) {
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
    last_measurement.adc_mv = adc_mv_current;

    last_sample_time_us = now;
    new_measurement_available = true;

    // adc_mv e offset incluidos no log para facilitar a calibracao:
    // rode o gear com I=0, leia adc_mv e ajuste CONFIG_AMMETER_OFFSET_MV.
    ESP_LOGI(TAG,
             "I=%.2fmA V=%.2fV P=%.2fmW Rem=%.1f%% adc_mv=%d (offset=%dmV)",
             last_measurement.current_ma, vbat, last_measurement.power_mw,
             last_measurement.battery_pct, adc_mv_current,
             CONFIG_AMMETER_OFFSET_MV);
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