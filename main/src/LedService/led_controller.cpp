#include "LedService/led_controller.hpp"
#include "LedService/led_driver.hpp"
#include "TaskPriorities.hpp"
#include "freertos/idf_additions.h"

static auto STATUS_LED_PIN = GPIO_NUM_8;

static QueueHandle_t led_queue;

static bool isInitialized = false;

void controller::led::init(void) {
    driver::led::init(STATUS_LED_PIN);

    led_queue = xQueueCreate(10, sizeof(led_cmd_t));

    xTaskCreate(controller::led::handler, "led_controller", 2048, NULL,
                static_cast<uint8_t>(task_priorities::TaskPrioritie::led),
                NULL);

    isInitialized = true;
}

void controller::led::handler(void *arg) {
    led_cmd_t cmd;

    for (;;) {
        if (xQueueReceive(led_queue, &cmd, portMAX_DELAY)) {
            switch (cmd.type) {
            case LED_CMD_SET:
                // On-board LED on these C3 boards is active-low: drive 0 to
                // light it.
                driver::led::set_gpio(STATUS_LED_PIN, !cmd.value);
                break;
            }
        }
    }
}

void controller::led::set_status(bool value) {
    if (!isInitialized) {
        return;
    }
    led_cmd_t cmd = {.type = LED_CMD_SET, .value = value};

    xQueueSend(led_queue, &cmd, portMAX_DELAY);
}
