#pragma once

#include <cstdint>

namespace service::application::reading {

void init();
void handler();

float get_last_reading();
bool has_new_reading();

} // namespace service::application::reading
