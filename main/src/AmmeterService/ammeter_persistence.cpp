#include "AmmeterService/ammeter_persistence.hpp"
#include "Application/nvs_service.hpp"

#include "esp_log.h"

#include <algorithm>

namespace service::ammeter::persistence {

namespace nvs = service::application::nvs;

static const char *TAG = "AMMETER_PERSIST";

static constexpr char KEY_MAH[] = "consumed_mah";
static constexpr char KEY_MWH[] = "consumed_mwh";

// Fração da capacidade que dispara uma nova gravação (1%).
static constexpr float PERSIST_FRACTION = 0.01f;

// consumed_mah da última gravação; também atualizado no load.
static float last_saved_mah = 0.0f;

static void derive(Measurement &m, float capacity_mah) {
    m.remaining_mah =
        std::min(std::max(capacity_mah - m.consumed_mah, 0.0f), capacity_mah);
    m.battery_pct = (m.remaining_mah / capacity_mah) * 100.0f;
}

void load(Measurement &m, float capacity_mah) {
    float mah = 0.0f;
    if (!nvs::get_float(KEY_MAH, &mah)) {
        last_saved_mah = m.consumed_mah; // sem persistência: mantém o init (cheio)
        ESP_LOGI(TAG, "Sem energia persistida — iniciando cheio");
        return;
    }

    m.consumed_mah = mah;

    float mwh = 0.0f;
    if (nvs::get_float(KEY_MWH, &mwh)) {
        m.consumed_mwh = mwh;
    }

    derive(m, capacity_mah);
    last_saved_mah = m.consumed_mah;
    ESP_LOGI(TAG, "Energia restaurada: consumed=%.1fmAh (%.1f%%)",
             m.consumed_mah, m.battery_pct);
}

void maybe_persist(const Measurement &m, float capacity_mah) {
    const float threshold = capacity_mah * PERSIST_FRACTION;
    if (m.consumed_mah - last_saved_mah < threshold) {
        return;
    }

    nvs::set_float(KEY_MAH, m.consumed_mah);
    nvs::set_float(KEY_MWH, m.consumed_mwh);
    last_saved_mah = m.consumed_mah;
    ESP_LOGI(TAG, "Energia gravada: consumed=%.1fmAh (%.1f%%)", m.consumed_mah,
             m.battery_pct);
}

} // namespace service::ammeter::persistence
