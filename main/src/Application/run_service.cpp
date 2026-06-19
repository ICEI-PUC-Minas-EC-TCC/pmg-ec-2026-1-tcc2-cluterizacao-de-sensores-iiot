#include "Application/run_service.hpp"
#include "Application/nvs_service.hpp"
#include "RtcService/rtc_service.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "RUN_SERVICE";

namespace service::application::run {

static constexpr char KEY_RUN[] = "run_id";
static char current[ID_MAX] = "UNKNOWN";

void init() {
    char buf[ID_MAX] = {};
    if (nvs::get_str(KEY_RUN, buf, sizeof(buf))) {
        strncpy(current, buf, sizeof(current) - 1);
        current[sizeof(current) - 1] = '\0';
    }
    ESP_LOGI(TAG, "run_id = %s", current);
}

const char *id() { return current; }

void set_and_persist(const char *new_id) {
    strncpy(current, new_id, sizeof(current) - 1);
    current[sizeof(current) - 1] = '\0';
    nvs::set_str(KEY_RUN, current);
    ESP_LOGI(TAG, "run_id set = %s", current);
}

std::string generate_now() {
    // RTC ja' e' a fonte de "measured_time"; reaproveita o formato.
    return service::rtc::get_current_time();
}

} // namespace service::application::run
