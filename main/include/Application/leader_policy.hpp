#pragma once

#include "Network/network_controller.hpp"
#include <vector>

namespace service::application::leader_policy {

// Build-time strategy selection. Exactly one of the macros below is set by
// Kconfig; this constant lets code switch on it without #ifdef noise at
// the call sites.
enum class Strategy {
    ROUND_ROBIN,
    ENERGY,
    ENERGY_COOLDOWN,
};

void init();

// Hook called when this node becomes leader, so cooldown can remember
// recent leadership.
void on_became_leader(controller::network::MacAddr leader);

// Decide the next leader from the current cluster snapshot. The current
// leader is passed explicitly so policies can exclude or down-rank it.
controller::network::MacAddr pick_next_leader(
    const std::vector<controller::network::MacAddr> &cluster,
    controller::network::MacAddr current_leader);

const char *strategy_name();

} // namespace service::application::leader_policy
