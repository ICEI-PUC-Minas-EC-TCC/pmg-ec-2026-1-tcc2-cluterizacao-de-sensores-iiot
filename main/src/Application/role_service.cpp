#include "Application/role_service.hpp"
#include "Network/network_service.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "utils.hpp"

#include <cstring>

static const char *TAG = "ROLE_SERVICE";

namespace service::application::role {

using controller::network::MacAddr;

static Role   current_role = Role::UNDECIDED;
static MacAddr leader_mac{};

// Minimum time to wait for peer discovery before forcing an election.
static constexpr uint32_t DISCOVERY_WINDOW_MS = 5000;

static MacAddr get_own_mac() {
    MacAddr mac{};
    esp_wifi_get_mac(WIFI_IF_STA, mac.data());
    return mac;
}

static void elect() {
    const auto &peers = service::network::get_known_peers();
    if (peers.empty()) {
        return;
    }

    MacAddr own_mac = get_own_mac();
    MacAddr smallest = own_mac;

    for (const auto &peer : peers) {
        if (memcmp(peer.data(), smallest.data(), 6) < 0) {
            smallest = peer;
        }
    }

    leader_mac = smallest;

    if (memcmp(own_mac.data(), smallest.data(), 6) == 0) {
        current_role = Role::LEADER;
        ESP_LOGI(TAG, "Role: LEADER (" MACSTR ")", MAC2STR(own_mac.data()));
    } else {
        current_role = Role::MEMBER;
        ESP_LOGI(TAG, "Role: MEMBER (leader: " MACSTR ")", MAC2STR(smallest.data()));
    }
}

void init() {
    current_role = Role::UNDECIDED;
    ESP_LOGI(TAG, "Waiting for peer discovery (%u ms)...", DISCOVERY_WINDOW_MS);
}

void on_peer_discovered() {
    if (current_role == Role::UNDECIDED) {
        elect();
    }
}

void handler() {
    if (current_role != Role::UNDECIDED) {
        return;
    }

    // Periodic fallback in case the first peer ping is missed.
    static utils::Timer discovery_timer;
    if (discovery_timer.hasElapsed(DISCOVERY_WINDOW_MS)) {
        elect();
        discovery_timer.reset();
    }
}

Role get_role() {
    return current_role;
}

bool is_leader() {
    return current_role == Role::LEADER;
}

MacAddr get_leader_mac() {
    return leader_mac;
}

} // namespace service::application::role
