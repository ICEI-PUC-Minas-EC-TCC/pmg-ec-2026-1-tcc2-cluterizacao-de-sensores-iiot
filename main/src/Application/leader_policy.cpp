#include "Application/leader_policy.hpp"
#include "Application/energy_service.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#include "utils.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

static const char *TAG = "LEADER_POLICY";

namespace service::application::leader_policy {

using controller::network::MacAddr;

#if defined(CONFIG_LEADER_POLICY_ENERGY)
static constexpr Strategy STRATEGY = Strategy::ENERGY;
static const char *STRATEGY_NAME   = "energy";
#elif defined(CONFIG_LEADER_POLICY_ENERGY_COOLDOWN)
static constexpr Strategy STRATEGY = Strategy::ENERGY_COOLDOWN;
static const char *STRATEGY_NAME   = "energy_cooldown";
#else
static constexpr Strategy STRATEGY = Strategy::ROUND_ROBIN;
static const char *STRATEGY_NAME   = "round_robin";
#endif

// Cooldown duration after a node holds leadership before it can be picked
// again. 20 s = 2 mandates with TERM_DURATION_MS = 10 s.
static constexpr uint32_t COOLDOWN_MS = 20'000;

static uint64_t mac_to_key(MacAddr mac) {
    uint64_t k = 0;
    for (int i = 0; i < 6; ++i) {
        k = (k << 8) | mac[i];
    }
    return k;
}

// Last time each MAC was leader. Empty entry == never (or evicted; we keep
// only the most recent, which is enough for the cooldown check).
static std::unordered_map<uint64_t, utils::Timer> last_leadership;

static bool in_cooldown(MacAddr mac) {
    auto it = last_leadership.find(mac_to_key(mac));
    if (it == last_leadership.end()) {
        return false;
    }
    return !it->second.hasElapsed(COOLDOWN_MS);
}

void init() {
    last_leadership.clear();
    ESP_LOGI(TAG, "Strategy: %s", STRATEGY_NAME);
}

void on_became_leader(MacAddr leader) {
    auto &t = last_leadership[mac_to_key(leader)];
    t.reset();
}

const char *strategy_name() {
    return STRATEGY_NAME;
}

// Sorted by MAC ascending; next leader is the entry after current_leader
// in the ring. Stable, predictable, ignores energy.
static MacAddr pick_round_robin(const std::vector<MacAddr> &cluster,
                                MacAddr current_leader) {
    auto it = std::find_if(cluster.begin(), cluster.end(),
                           [&](const MacAddr &m) {
                               return memcmp(m.data(), current_leader.data(), 6) == 0;
                           });
    size_t idx = (it != cluster.end()) ? (size_t)(it - cluster.begin()) : 0;
    return cluster[(idx + 1) % cluster.size()];
}

// Highest residual energy wins. Ties broken by MAC order. Falls back to
// round-robin when no peer in the cluster has a fresh energy sample.
static MacAddr pick_energy(const std::vector<MacAddr> &cluster,
                           MacAddr current_leader,
                           bool exclude_in_cooldown) {
    MacAddr  best{};
    uint32_t best_energy = 0;
    bool     found       = false;

    MacAddr own_mac{};
    esp_wifi_get_mac(WIFI_IF_STA, own_mac.data());

    for (const auto &mac : cluster) {
        if (exclude_in_cooldown && in_cooldown(mac)) {
            continue;
        }

        uint32_t energy = 0;
        bool     valid  = true;
        if (memcmp(mac.data(), own_mac.data(), 6) == 0) {
            energy = energy::get_residual();
        } else {
            energy = energy::get_peer_residual(mac, &valid);
        }
        if (!valid) {
            continue;
        }

        if (!found || energy > best_energy) {
            best        = mac;
            best_energy = energy;
            found       = true;
        }
    }

    if (!found) {
        ESP_LOGW(TAG, "No valid energy samples (cooldown=%d), falling back to RR",
                 exclude_in_cooldown);
        return pick_round_robin(cluster, current_leader);
    }
    return best;
}

MacAddr pick_next_leader(const std::vector<MacAddr> &cluster, MacAddr current_leader) {
    if (cluster.empty()) {
        return current_leader;
    }

    switch (STRATEGY) {
    case Strategy::ENERGY:
        return pick_energy(cluster, current_leader, /*exclude_in_cooldown=*/false);
    case Strategy::ENERGY_COOLDOWN:
        return pick_energy(cluster, current_leader, /*exclude_in_cooldown=*/true);
    case Strategy::ROUND_ROBIN:
    default:
        return pick_round_robin(cluster, current_leader);
    }
}

} // namespace service::application::leader_policy
