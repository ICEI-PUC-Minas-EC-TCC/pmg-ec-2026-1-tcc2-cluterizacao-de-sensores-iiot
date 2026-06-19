#pragma once

#include <cstdint>

namespace service::application::nvs {

void init();

// Persistência no namespace "energy" (usado pela bateria do ammeter e pelo
// orçamento do energy_service). Escritas são raras (gravação por limiar).
bool get_float(const char *key, float *out);
void set_float(const char *key, float value);
bool get_u32(const char *key, uint32_t *out);
void set_u32(const char *key, uint32_t value);
void erase_energy();

} // namespace service::application::nvs