#include "Application/button_service.hpp"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "hal/gpio_types.h"
#include "soc/gpio_num.h"
#include "utils.hpp"

namespace service::application::button {

static const char *TAG = "BUTTON_SERVICE";

constexpr gpio_num_t nBOOT_BUTTON = GPIO_NUM_9;

// Hold BOOT this long to force a soft reset. Useful on the bench when a node
// gets stuck in a Wi-Fi scan loop or stale role state and the EN button on
// the C3 SuperMini clone is too small to reach reliably.
constexpr uint32_t LONG_PRESS_MS = 2000;

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
    static bool restart_armed = false;

    if (!poll_timer.hasElapsed(50)) {
        return;
    }
    poll_timer.reset();

    // BOOT is active-low on the C3: the board pulls the line high through an
    // external resistor and pressing the button shorts it to GND.
    bool pressed = (gpio_get_level(nBOOT_BUTTON) == 0);

    if (pressed && !was_pressed) {
        press_timer.reset();
        restart_armed = true;
    }

    if (pressed && restart_armed && press_timer.hasElapsed(LONG_PRESS_MS)) {
        ESP_LOGW(TAG, "BOOT held %u ms — restarting", LONG_PRESS_MS);
        restart_armed = false;
        esp_restart();
    }

    if (!pressed) {
        restart_armed = false;
    }

    was_pressed = pressed;
}

} // namespace service::application::button
