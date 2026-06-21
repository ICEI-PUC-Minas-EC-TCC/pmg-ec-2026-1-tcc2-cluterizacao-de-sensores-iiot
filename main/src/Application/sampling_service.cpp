#include "Application/sampling_service.hpp"
#include "utils.hpp"

namespace service::application::sampling {

static constexpr uint32_t SAMPLING_INTERVAL_MS = 2000;

static bool new_sample_available = false;

void init() {
    new_sample_available = false;
}

void handler() {
    static utils::Timer sampling_timer;

    if (!sampling_timer.hasElapsed(SAMPLING_INTERVAL_MS)) {
        return;
    }

    new_sample_available = true;
    sampling_timer.reset();
}

bool has_new_sample() {
    if (new_sample_available) {
        new_sample_available = false;
        return true;
    }
    return false;
}

} // namespace service::application::sampling
