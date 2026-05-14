#pragma once

namespace service::ammeter {

struct Measurement {
    float current_ma;
    float power_mw;
    float consumed_mah;
    float consumed_mwh;
    float remaining_mah;
    float battery_pct;
    int adc_raw;
    int adc_mv;
};

void init();
void handler();

Measurement get_last_measurement();
bool has_new_measurement();

} // namespace service::ammeter
