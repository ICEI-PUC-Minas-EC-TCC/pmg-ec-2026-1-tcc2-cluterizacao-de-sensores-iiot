#include "Application/reset_service.hpp"
#include "Application/energy_service.hpp"
#include "Application/role_service.hpp"
#include "Application/run_service.hpp"
#include "AmmeterService/ammeter_persistence.hpp"
#include "AmmeterService/ammeter_service.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "RESET_SERVICE";

namespace service::application::reset {

// Perfil escalonado por posicao de MAC (Cenario B). Nos alem da lista repetem
// o ultimo nivel.
static constexpr float STAGGER_LEVELS[] = {100.0f, 85.0f, 70.0f, 55.0f, 40.0f};
static constexpr int   STAGGER_N = sizeof(STAGGER_LEVELS) / sizeof(STAGGER_LEVELS[0]);

void apply_and_restart(service::network::ResetScenario scenario,
                       const char *run_id) {
    float pct = 100.0f;
    if (scenario == service::network::ResetScenario::STAGGERED) {
        int rank = service::application::role::own_rank();
        if (rank >= STAGGER_N) rank = STAGGER_N - 1;
        pct = STAGGER_LEVELS[rank];
        ESP_LOGW(TAG, "STAGGERED reset: rank=%d pct=%.0f", rank, pct);
    } else {
        ESP_LOGW(TAG, "FULL reset: pct=100");
    }

    service::application::run::set_and_persist(run_id);
    service::application::energy::persist_reset_pct(pct);
    service::ammeter::persistence::persist_reset_pct(
        pct, service::ammeter::get_battery_capacity_mah());

    // Flush de logs/NVS antes do restart.
    vTaskDelay(200 / portTICK_PERIOD_MS);
    esp_restart();
}

} // namespace service::application::reset
