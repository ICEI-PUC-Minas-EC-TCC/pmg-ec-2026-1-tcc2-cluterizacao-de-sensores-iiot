#pragma once

#include "soc/gpio_num.h"
#include <stdbool.h>
#include <stdint.h>

namespace driver::led {

void init(gpio_num_t pin);
void set_gpio(gpio_num_t pin, bool state);

} // namespace driver::led