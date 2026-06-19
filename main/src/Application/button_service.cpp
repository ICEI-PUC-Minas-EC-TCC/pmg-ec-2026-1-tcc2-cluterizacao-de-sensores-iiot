#include "Application/button_service.hpp"
#include "Application/nvs_service.hpp"
#include "Network/network_service.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include "utils.hpp"

namespace service::application::button {

static const char *TAG = "BUTTON_SERVICE";

constexpr gpio_num_t nBOOT_BUTTON = GPIO_NUM_9;

// Gestos no BOOT, decididos na SOLTURA (precisamos distinguir as durações).
// Útil na bancada: a EN do clone C3 SuperMini é pequena demais p/ alcançar
// com confiabilidade.
//   2–5 s  -> soft reset (reinicia)
//   > 5 s  -> zera a energia persistida (bateria + orçamento) e reinicia
constexpr uint32_t RESTART_PRESS_MS = 2000;
constexpr uint32_t RESET_PRESS_MS = 5000;

void init() {
    gpio_config_t config = {.pin_bit_mask = (1ULL << nBOOT_BUTTON),
                            .mode = GPIO_MODE_INPUT,
                            .pull_up_en = GPIO_PULLUP_DISABLE,
                            .pull_down_en = GPIO_PULLDOWN_DISABLE,
                            .intr_type = GPIO_INTR_DISABLE};

    gpio_config(&config);
}

void handler() {
    static utils::Timer poll_timer;
    static utils::Timer press_timer;
    static bool was_pressed = false;

    if (!poll_timer.hasElapsed(50)) {
        return;
    }
    poll_timer.reset();

    // BOOT is active-low on the C3: the board pulls the line high through an
    // external resistor and pressing the button shorts it to GND.
    bool pressed = (gpio_get_level(nBOOT_BUTTON) == 0);

    if (pressed && !was_pressed) {
        press_timer.reset(); // começou a pressionar
    }

    // Decide o gesto na soltura, pela duração do hold.
    if (!pressed && was_pressed) {
        if (press_timer.hasElapsed(RESET_PRESS_MS)) {
            ESP_LOGW(TAG,
                     "BOOT segurado >= %u ms — reset de energia em REDE e "
                     "reiniciando",
                     RESET_PRESS_MS);
            // Propaga o reset para todos os nos: broadcast repetido (ESP-NOW
            // nao tem ACK, entao repetimos para cobrir perdas). O delay entre
            // envios tambem garante o flush do TX antes do esp_restart local.
            for (int i = 0; i < 4; ++i) {
                service::network::send_reset_energy_broadcast();
                vTaskDelay(150 / portTICK_PERIOD_MS);
            }
            service::application::nvs::erase_energy();
            esp_restart();
        } else if (press_timer.hasElapsed(RESTART_PRESS_MS)) {
            ESP_LOGW(TAG, "BOOT segurado >= %u ms — reiniciando",
                     RESTART_PRESS_MS);
            esp_restart();
        }
    }

    was_pressed = pressed;
}

} // namespace service::application::button
