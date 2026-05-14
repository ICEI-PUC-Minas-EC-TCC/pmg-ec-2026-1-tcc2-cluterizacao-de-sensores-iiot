#include "AmmeterService/ammeter_service.hpp"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>

namespace service::ammeter {

#ifndef CONFIG_AMMETER_SAMPLE_INTERVAL_MS
#define CONFIG_AMMETER_SAMPLE_INTERVAL_MS 1000
#endif
#ifndef CONFIG_AMMETER_ADC_AVG_SAMPLES
#define CONFIG_AMMETER_ADC_AVG_SAMPLES 32
#endif
#ifndef CONFIG_AMMETER_SHUNT_OHMS_X1000
#define CONFIG_AMMETER_SHUNT_OHMS_X1000 100
#endif
#ifndef CONFIG_AMMETER_AMPLIFIER_GAIN_X100
#define CONFIG_AMMETER_AMPLIFIER_GAIN_X100 1100
#endif
#ifndef CONFIG_AMMETER_BATTERY_VOLTAGE_MV
#define CONFIG_AMMETER_BATTERY_VOLTAGE_MV 3700
#endif
#ifndef CONFIG_AMMETER_BATTERY_CAPACITY_MAH
#define CONFIG_AMMETER_BATTERY_CAPACITY_MAH 1000
#endif
#ifndef CONFIG_AMMETER_ADC_DEFAULT_VREF_MV
#define CONFIG_AMMETER_ADC_DEFAULT_VREF_MV 1100
#endif

static const char *TAG = "AMMETER_SERVICE";

static constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;
static constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_0;
static constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;

static constexpr uint32_t SAMPLE_INTERVAL_MS = CONFIG_AMMETER_SAMPLE_INTERVAL_MS;
static constexpr int AVG_SAMPLES = CONFIG_AMMETER_ADC_AVG_SAMPLES;

static constexpr float SHUNT_OHMS = static_cast<float>(CONFIG_AMMETER_SHUNT_OHMS_X1000) / 1000.0f;
static constexpr float AMPLIFIER_GAIN = static_cast<float>(CONFIG_AMMETER_AMPLIFIER_GAIN_X100) / 100.0f;
static constexpr float BATTERY_VOLTAGE_V = static_cast<float>(CONFIG_AMMETER_BATTERY_VOLTAGE_MV) / 1000.0f;
static constexpr float BATTERY_CAPACITY_MAH = static_cast<float>(CONFIG_AMMETER_BATTERY_CAPACITY_MAH);
static constexpr float ADC_DEFAULT_VREF_V = static_cast<float>(CONFIG_AMMETER_ADC_DEFAULT_VREF_MV) / 1000.0f;

static adc_oneshot_unit_handle_t adc_handle = nullptr;
static adc_cali_handle_t adc_cali_handle = nullptr;
static bool adc_cali_enabled = false;
static bool initialized = false;
static bool new_measurement_available = false;

static Measurement last_measurement{};
static int64_t last_sample_time_us = 0;

static int adc_raw_to_mv(int raw) {
    int voltage_mv = 0;

    if (adc_cali_enabled) {
        if (adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage_mv) == ESP_OK) {
            return voltage_mv;
        }
        ESP_LOGW(TAG, "ADC calibration conversion failed, using fallback Vref");
    }

    // Fallback linear conversion when calibration is unavailable.
    voltage_mv = static_cast<int>((static_cast<float>(raw) / 4095.0f) * ADC_DEFAULT_VREF_V * 1000.0f);
    return voltage_mv;
}

void init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t channel_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &channel_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle) == ESP_OK) {
        adc_cali_enabled = true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .default_vref = CONFIG_AMMETER_ADC_DEFAULT_VREF_MV,
    };
    if (adc_cali_create_scheme_line_fitting(&cali_cfg, &adc_cali_handle) == ESP_OK) {
        adc_cali_enabled = true;
    }
#endif

    last_measurement = {};
    last_measurement.remaining_mah = BATTERY_CAPACITY_MAH;
    last_measurement.battery_pct = 100.0f;

    last_sample_time_us = esp_timer_get_time();
    initialized = true;

    ESP_LOGI(TAG,
             "Initialized | shunt=%.3f ohm gain=%.2f Vbat=%.3fV capacity=%.0fmAh calibration=%s",
             SHUNT_OHMS,
             AMPLIFIER_GAIN,
             BATTERY_VOLTAGE_V,
             BATTERY_CAPACITY_MAH,
             adc_cali_enabled ? "on" : "off");
}

void handler() {
    if (!initialized) {
        return;
    }

    const int64_t now_us = esp_timer_get_time();
    const int64_t elapsed_us = now_us - last_sample_time_us;

    if (elapsed_us < static_cast<int64_t>(SAMPLE_INTERVAL_MS) * 1000LL) {
        return;
    }

    int raw_sum = 0;
    for (int i = 0; i < AVG_SAMPLES; ++i) {
        int raw = 0;
        if (adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw) == ESP_OK) {
            raw_sum += raw;
        }
    }

    const int raw_avg = raw_sum / std::max(AVG_SAMPLES, 1);
    const int amp_out_mv = adc_raw_to_mv(raw_avg);

    const float amp_out_v = static_cast<float>(amp_out_mv) / 1000.0f;
    const float shunt_v = amp_out_v / AMPLIFIER_GAIN;
    const float current_a = std::max(shunt_v / SHUNT_OHMS, 0.0f);
    const float power_w = current_a * BATTERY_VOLTAGE_V;

    const float elapsed_h = static_cast<float>(elapsed_us) / 3600000000.0f;
    const float delta_mah = current_a * 1000.0f * elapsed_h;
    const float delta_mwh = power_w * 1000.0f * elapsed_h;

    last_measurement.current_ma = current_a * 1000.0f;
    last_measurement.power_mw = power_w * 1000.0f;
    last_measurement.consumed_mah += delta_mah;
    last_measurement.consumed_mwh += delta_mwh;
    last_measurement.remaining_mah = std::max(BATTERY_CAPACITY_MAH - last_measurement.consumed_mah, 0.0f);

    if (BATTERY_CAPACITY_MAH > 0.0f) {
        last_measurement.battery_pct = (last_measurement.remaining_mah / BATTERY_CAPACITY_MAH) * 100.0f;
    } else {
        last_measurement.battery_pct = 0.0f;
    }

    last_measurement.adc_raw = raw_avg;
    last_measurement.adc_mv = amp_out_mv;

    last_sample_time_us = now_us;
    new_measurement_available = true;

    ESP_LOGI(TAG,
             "I=%.2f mA P=%.2f mW Consumed=%.3f mAh Remaining=%.2f%% (raw=%d, mv=%d)",
             last_measurement.current_ma,
             last_measurement.power_mw,
             last_measurement.consumed_mah,
             last_measurement.battery_pct,
             last_measurement.adc_raw,
             last_measurement.adc_mv);
}

Measurement get_last_measurement() {
    return last_measurement;
}

bool has_new_measurement() {
    if (!new_measurement_available) {
        return false;
    }

    new_measurement_available = false;
    return true;
}

} // namespace service::ammeter
