#pragma once
#include "Network/network_service.hpp"

namespace service::application::reset {

// Aplica o cenario de reset: grava no NVS a bateria/orcamento alvo (cheio ou
// escalonado pela posicao de MAC) e o run_id, depois reinicia.
void apply_and_restart(service::network::ResetScenario scenario,
                       const char *run_id);

} // namespace service::application::reset
