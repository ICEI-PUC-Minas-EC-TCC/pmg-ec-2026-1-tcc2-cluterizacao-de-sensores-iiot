#pragma once

#include "FreeRTOSConfig.h"
#include "freertos/FreeRTOSConfig_arch.h"
#include <cstdint>
namespace task_priorities {

enum class TaskPrioritie : uint8_t {
    application = configMAX_PRIORITIES - 2,
    led = 0,
    mqtt = configMAX_PRIORITIES - 1,
    network = configMAX_PRIORITIES - 1,
    ammeter = configMAX_PRIORITIES - 1,
};

}