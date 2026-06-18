#pragma once

#include <cstdint>

namespace service::application::sampling {

void init();
void handler();

bool has_new_sample();

} // namespace service::application::sampling
