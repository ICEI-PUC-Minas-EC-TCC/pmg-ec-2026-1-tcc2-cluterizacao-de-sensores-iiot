#pragma once

#include "esp_timer.h"
#include <chrono>

namespace utils {

#define CHECK_INITIALIZED()                                                    \
    if (!isInitialized) {                                                      \
        return;                                                                \
    }

struct Timer {
    unsigned long last = 0;

    bool hasElapsed(unsigned long interval) {
        unsigned long now = esp_timer_get_time() / 1000;
        if (now - last >= interval) {
            return true;
        }
        return false;
    }

    void reset() {
        last = esp_timer_get_time() / 1000;
    }
};

} // namespace utils
