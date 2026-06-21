#pragma once
#include <cstddef>
#include <string>

namespace service::application::run {

static constexpr size_t ID_MAX = 24; // "2026-06-20 14:00:00" + folga

void init();
const char *id();
void set_and_persist(const char *id);
std::string generate_now();

} // namespace service::application::run
