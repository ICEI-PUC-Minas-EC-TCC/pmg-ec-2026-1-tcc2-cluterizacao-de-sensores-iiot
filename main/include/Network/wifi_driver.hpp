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

}
