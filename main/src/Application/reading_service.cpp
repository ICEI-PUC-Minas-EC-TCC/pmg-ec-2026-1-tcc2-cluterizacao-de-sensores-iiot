#include "Application/reading_service.hpp"
#include "esp_log.h"
#include "utils.hpp"

static const char *TAG = "READING_SERVICE";

namespace service::application::reading {

static constexpr uint32_t READING_INTERVAL_MS = 5000;
static constexpr float    TEMP_MIN  = 20.0f;
static constexpr float    TEMP_MAX  = 29.0f;
static constexpr float    TEMP_STEP =  1.0f;

static float last_reading          = TEMP_MIN;
static bool  new_reading_available = false;

void init() {
    last_reading          = TEMP_MIN;
    new_reading_available = false;
}

void handler() {
    static utils::Timer reading_timer;

    if (!reading_timer.hasElapsed(READING_INTERVAL_MS)) {
        return;
    }

    last_reading += TEMP_STEP;
    if (last_reading > TEMP_MAX) {
        last_reading = TEMP_MIN;
    }

    new_reading_available = true;
    ESP_LOGI(TAG, "New reading: %.1f C", last_reading);
    reading_timer.reset();
}

float get_last_reading() {
    return last_reading;
}

bool has_new_reading() {
    if (new_reading_available) {
        new_reading_available = false;
        return true;
    }
    return false;
}

} // namespace service::application::reading
