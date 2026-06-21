#include "LedService/led_controller.hpp"
#include "TaskPriorities.hpp"
#include "freertos/idf_additions.h"
#include "sdkconfig.h"
#include <cstdint>

#ifdef CONFIG_AMMETER_BACKEND_INA219

#include "AmmeterService/ammeter_persistence.hpp"
#include "AmmeterService/ammeter_service.hpp"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cmath>

namespace service::ammeter {

static const char *TAG = "AMMETER_INA219";

// ---------- INA219 registers ----------
static constexpr uint8_t REG_CONFIG = 0x00;
static constexpr uint8_t REG_SHUNT_V = 0x01;
static constexpr uint8_t REG_BUS_V = 0x02;
static constexpr uint8_t REG_CURRENT = 0x04;
static constexpr uint8_t REG_CALIBRATION = 0x05;

// Config: 16V BRNG, PGA=÷2 (±80mV), 12-bit/16-samples BADC+SADC, modo contínuo
// bits: RST=0 | res=0 | BRNG=0 | PGA=01 | BADC=1100 | SADC=1100 | MODE=111
static constexpr uint16_t INA219_CONFIG = 0x0E67;

// Bus Voltage register flags
static constexpr uint16_t BUS_V_CNVR = (1u << 1); // Conversion Ready
static constexpr uint16_t BUS_V_OVF = (1u << 0);  // Math Overflow

// ---------- Kconfig ----------
static constexpr float SHUNT_OHMS =
    CONFIG_AMMETER_INA219_SHUNT_OHMS_X1000 / 1000.0f;
static constexpr float MAX_CURRENT_A =
    CONFIG_AMMETER_INA219_MAX_CURRENT_MA / 1000.0f;
static constexpr float BATTERY_CAPACITY_MAH =
    CONFIG_AMMETER_BATTERY_CAPACITY_MAH;
static constexpr uint32_t SAMPLE_INTERVAL_MS =
    CONFIG_AMMETER_SAMPLE_INTERVAL_MS;
static constexpr float EMA_ALPHA = 0.2f;

// ---------- Estado ----------
static i2c_master_bus_handle_t bus_handle = nullptr;
static i2c_master_dev_handle_t dev_handle = nullptr;
static bool initialized = false;
static bool new_measurement_available = false;
static Measurement last_measurement{};
static int64_t last_sample_time_us = 0;
static float current_lsb_ma = 0.0f;
static float filtered_ma = 0.0f;

// ---------- I2C helpers ----------

static esp_err_t write_reg(uint8_t reg, uint16_t value) {
    uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF)};
    return i2c_master_transmit(dev_handle, buf, sizeof(buf), 100);
}

static esp_err_t read_reg(uint8_t reg, int16_t *out) {
    uint8_t rx[2];
    esp_err_t ret =
        i2c_master_transmit_receive(dev_handle, &reg, 1, rx, sizeof(rx), 100);
    if (ret == ESP_OK)
        *out = (int16_t)((rx[0] << 8) | rx[1]);
    return ret;
}

// ---------- init ----------

void init() {

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = (gpio_num_t)CONFIG_AMMETER_INA219_SDA_GPIO,
        .scl_io_num = (gpio_num_t)CONFIG_AMMETER_INA219_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {.enable_internal_pullup = true},
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CONFIG_AMMETER_INA219_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(
        i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle));

    // Calibração: Current_LSB = Max_I / 32768
    // Cal = trunc(0.04096 / (Current_LSB_A * R_shunt))
    current_lsb_ma = (MAX_CURRENT_A * 1000.0f) / 32768.0f;
    float current_lsb_a = current_lsb_ma / 1000.0f;
    float cal_f = 0.04096f / (current_lsb_a * SHUNT_OHMS);

    if (cal_f > 65535.0f) {
        ESP_LOGE(TAG,
                 "Overflow na calibracao INA219 (cal=%.0f) — reduza "
                 "AMMETER_INA219_MAX_CURRENT_MA ou aumente "
                 "AMMETER_INA219_SHUNT_OHMS_X1000.",
                 cal_f);
        return;
    }
    uint16_t cal = (uint16_t)cal_f;
    if (cal == 0) {
        ESP_LOGE(TAG, "Calibracao INA219 resultou em zero — aumente "
                      "AMMETER_INA219_MAX_CURRENT_MA ou reduza "
                      "AMMETER_INA219_SHUNT_OHMS_X1000.");
        return;
    }

    // Wait hardware stabilize.
    vTaskDelay(100 / portTICK_PERIOD_MS);

    esp_err_t err = write_reg(REG_CONFIG, INA219_CONFIG);
    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Falha ao configurar INA219 (SDA=%d SCL=%d addr=0x%02X): %s. "
                 "Verifique a fiacao e o endereco I2C.",
                 CONFIG_AMMETER_INA219_SDA_GPIO, CONFIG_AMMETER_INA219_SCL_GPIO,
                 CONFIG_AMMETER_INA219_ADDR, esp_err_to_name(err));
        return;
    }

    err = write_reg(REG_CALIBRATION, cal);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao escrever calibracao INA219: %s",
                 esp_err_to_name(err));
        return;
    }

    filtered_ma = 0.0f;
    last_measurement = {};
    last_measurement.remaining_mah = BATTERY_CAPACITY_MAH;
    last_measurement.battery_pct = 100.0f;
    persistence::load(last_measurement, BATTERY_CAPACITY_MAH);
    last_sample_time_us = esp_timer_get_time();
    initialized = true;

    ESP_LOGI(TAG,
             "INA219 OK (SDA=%d SCL=%d addr=0x%02X shunt=%.3fOhm "
             "max=%.0fmA cal=%u lsb=%.4fmA)",
             CONFIG_AMMETER_INA219_SDA_GPIO, CONFIG_AMMETER_INA219_SCL_GPIO,
             CONFIG_AMMETER_INA219_ADDR, SHUNT_OHMS, MAX_CURRENT_A * 1000.0f,
             cal, current_lsb_ma);

    xTaskCreate(handler, "Ammeter", 4096, NULL,
                static_cast<uint8_t>(task_priorities::TaskPrioritie::ammeter),
                NULL);

    // controller::led::set_status(true);
}

// ---------- handler ----------

void handler(void *arg) {
    for (;;) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        const int64_t now = esp_timer_get_time();
        if ((now - last_sample_time_us) < (int64_t)SAMPLE_INTERVAL_MS * 1000)
            continue;

        // Lê o registrador de tensão de barramento primeiro para verificar os
        // flags CNVR (bit 1) e OVF (bit 0) antes de usar os dados.
        int16_t raw_bus = 0;
        if (read_reg(REG_BUS_V, &raw_bus) != ESP_OK) {
            ESP_LOGW(TAG, "Falha na leitura I2C (REG_BUS_V)");
            continue;
        }

        uint16_t bus_flags = (uint16_t)raw_bus;

        if (!(bus_flags & BUS_V_CNVR)) {
            // Conversão ainda em andamento; dados do ciclo anterior — pula.
            continue;
        }

        if (bus_flags & BUS_V_OVF) {
            // Corrente excede MAX_CURRENT_A; registradores Current/Power
            // inválidos.
            ESP_LOGW(
                TAG,
                "INA219 OVF: corrente acima do limite configurado (max=%.0fmA)",
                MAX_CURRENT_A * 1000.0f);
            last_sample_time_us = now;
            continue;
        }

        int16_t raw_current = 0;
        if (read_reg(REG_CURRENT, &raw_current) != ESP_OK) {
            ESP_LOGW(TAG, "Falha na leitura I2C (REG_CURRENT)");
            continue;
        }

        float current_ma = raw_current * current_lsb_ma;

        // Tensão de barramento: bits [15:3], LSB = 4 mV.
        // Cast para uint16_t antes do shift para evitar extensão de sinal.
        float vbus_v = ((uint16_t)raw_bus >> 3) * 0.004f;

        // Zona morta
        constexpr float dead_zone_ma = CONFIG_AMMETER_DEAD_ZONE_UA / 1000.0f;
        if (dead_zone_ma > 0.0f && std::fabs(current_ma) < dead_zone_ma)
            current_ma = 0.0f;

        // Filtro EMA
        filtered_ma = EMA_ALPHA * current_ma + (1.0f - EMA_ALPHA) * filtered_ma;

        // Potência e integração
        float power_mw = filtered_ma * vbus_v;
        float elapsed_h = (now - last_sample_time_us) / 3600000000.0f;
        float delta_mah = filtered_ma * elapsed_h;
        float delta_mwh = power_mw * elapsed_h;

        last_measurement.current_ma = filtered_ma;
        last_measurement.power_mw = power_mw;
        last_measurement.consumed_mah += delta_mah;
        last_measurement.consumed_mwh += delta_mwh;

        // Clamp em ambos os lados: carregamento não pode elevar acima da
        // capacidade, descarga não pode cair abaixo de zero.
        last_measurement.remaining_mah = std::min(
            std::max(BATTERY_CAPACITY_MAH - last_measurement.consumed_mah,
                     0.0f),
            BATTERY_CAPACITY_MAH);
        last_measurement.battery_pct =
            (last_measurement.remaining_mah / BATTERY_CAPACITY_MAH) * 100.0f;

        // Reusa campos ADC para debug: raw_current e bus_voltage
        last_measurement.adc_raw = raw_current;
        last_measurement.adc_mv = (int)(vbus_v * 1000.0f);

        last_sample_time_us = now;
        new_measurement_available = true;

        persistence::maybe_persist(last_measurement, BATTERY_CAPACITY_MAH);

        ESP_LOGI(TAG, "I=%.2fmA V=%.3fV P=%.2fmW Rem=%.1f%%", filtered_ma,
                 vbus_v, power_mw, last_measurement.battery_pct);
    }
}

// ---------- getters ----------

Measurement get_last_measurement() {
    return last_measurement;
}

bool has_new_measurement() {
    if (!new_measurement_available)
        return false;
    new_measurement_available = false;
    return true;
}

float get_battery_capacity_mah() { return BATTERY_CAPACITY_MAH; }

} // namespace service::ammeter

#endif // CONFIG_AMMETER_BACKEND_INA219
