#pragma once

#include "esp_wifi.h"

namespace driver::wifi {

void init();

// Associate with the configured AP. Idempotent: safe to call when already
// connected or while a previous attempt is still in progress.
void connect();

// Disassociate from the AP. The Wi-Fi stack stays started so ESP-NOW keeps
// working; only the station association is torn down.
void disconnect();

// While disassociated, the radio's channel can drift due to driver-internal
// activity (background scans, power events). Call this periodically from the
// main loop to re-pin the ESP-NOW channel; logs a warning whenever the
// channel was actually corrected.
void keep_channel_pinned();

}
