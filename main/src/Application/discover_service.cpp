#include "Application/discover_service.hpp"
#include "Network/network_service.hpp"
#include "esp_log.h"
#include "utils.hpp"

namespace service::application::discover {

void init() {}

void handler() {
    static utils::Timer discover_timer;

    if (discover_timer.hasElapsed(1000)) {
        ESP_LOGI(__FUNCTION__, "Broadcasting presence...");
        service::network::ping_broadcast();
        discover_timer.reset();
    }
}

} // namespace service::application::discover
