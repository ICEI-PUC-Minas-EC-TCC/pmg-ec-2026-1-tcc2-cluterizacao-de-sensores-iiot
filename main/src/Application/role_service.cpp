#include "Application/role_service.hpp"
#include "Network/network_service.hpp"
#include "Network/wifi_driver.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "utils.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

static const char *TAG = "ROLE_SERVICE";

namespace service::application::role {

using controller::network::MacAddr;

static Role    current_role = Role::UNDECIDED;
static MacAddr leader_mac{};

// Minimum time to wait for peer discovery before forcing an election.
static constexpr uint32_t DISCOVERY_WINDOW_MS = 5000;

// Duration each node holds the leader role before rotating (Round-Robin).
static constexpr uint32_t TERM_DURATION_MS = 30000;

// ROTATE retransmission to compensate for ESP-NOW broadcast packet loss.
// With ~20% reception rate, 10 retries at 200ms gives ~89% delivery probability.
static constexpr uint8_t  ROTATE_RETRIES  = 10;
static constexpr uint32_t ROTATE_RETRY_MS = 200;

static utils::Timer term_timer;

// Step-down state: retransmit ROTATE before committing to MEMBER role.
static bool    stepping_down       = false;
static uint8_t rotate_retries_left = 0;
static MacAddr pending_next_leader{};
static utils::Timer rotate_retry_timer;

static MacAddr get_own_mac() {
    MacAddr mac{};
    esp_wifi_get_mac(WIFI_IF_STA, mac.data());
    return mac;
}

// Returns all cluster nodes (self + known peers) sorted by MAC ascending.
static std::vector<MacAddr> sorted_cluster() {
    const auto &peers = service::network::get_known_peers();
    std::vector<MacAddr> nodes;
    nodes.reserve(peers.size() + 1);
    nodes.push_back(get_own_mac());
    for (const auto &p : peers) {
        nodes.push_back(p);
    }
    std::sort(nodes.begin(), nodes.end(), [](const MacAddr &a, const MacAddr &b) {
        return memcmp(a.data(), b.data(), 6) < 0;
    });
    return nodes;
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
        term_timer.reset();
        driver::wifi::connect();
        ESP_LOGI(TAG, "Role: LEADER (" MACSTR ")", MAC2STR(own_mac.data()));
    } else {
        current_role = Role::MEMBER;
        driver::wifi::disconnect();
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

void on_rotate_received(MacAddr next_leader) {
    // ROTATE is retransmitted up to ROTATE_RETRIES times by the sender to
    // compensate for broadcast loss. Each retx must be idempotent: do not
    // re-log nor reset the new leader's term timer when we already applied
    // this rotation.
    if (current_role != Role::UNDECIDED &&
        memcmp(leader_mac.data(), next_leader.data(), 6) == 0) {
        return;
    }

    MacAddr own_mac = get_own_mac();

    leader_mac = next_leader;

    if (memcmp(own_mac.data(), next_leader.data(), 6) == 0) {
        current_role = Role::LEADER;
        term_timer.reset();
        driver::wifi::connect();
        ESP_LOGI(TAG, "Role: LEADER (rotation, " MACSTR ")", MAC2STR(own_mac.data()));
    } else {
        current_role = Role::MEMBER;
        driver::wifi::disconnect();
        ESP_LOGI(TAG, "Role: MEMBER (new leader: " MACSTR ")", MAC2STR(next_leader.data()));
    }
}

void handler() {
    if (current_role == Role::UNDECIDED) {
        // Periodic fallback in case the first peer ping is missed.
        static utils::Timer discovery_timer;
        if (discovery_timer.hasElapsed(DISCOVERY_WINDOW_MS)) {
            elect();
            discovery_timer.reset();
        }
        return;
    }

    // Retransmit ROTATE until all attempts are exhausted, then step down.
    if (stepping_down) {
        if (!rotate_retry_timer.hasElapsed(ROTATE_RETRY_MS)) {
            return;
        }
        if (rotate_retries_left > 0) {
            service::network::send_rotate(pending_next_leader);
            rotate_retries_left--;
            rotate_retry_timer.reset();
        } else {
            stepping_down = false;
            leader_mac    = pending_next_leader;
            current_role  = Role::MEMBER;
            driver::wifi::disconnect();
            ESP_LOGI(TAG, "Role: MEMBER (stepped down)");
        }
        return;
    }

    if (current_role != Role::LEADER) {
        return;
    }

    if (!term_timer.hasElapsed(TERM_DURATION_MS)) {
        return;
    }

    auto    nodes   = sorted_cluster();
    MacAddr own_mac = get_own_mac();

    auto it = std::find_if(nodes.begin(), nodes.end(), [&](const MacAddr &m) {
        return memcmp(m.data(), own_mac.data(), 6) == 0;
    });

    size_t  idx  = (it != nodes.end()) ? (size_t)(it - nodes.begin()) : 0;
    MacAddr next = nodes[(idx + 1) % nodes.size()];

    ESP_LOGI(TAG, "Term expired — rotating to " MACSTR, MAC2STR(next.data()));

    // First transmission; remaining retries handled on subsequent handler calls.
    service::network::send_rotate(next);
    pending_next_leader = next;
    rotate_retries_left = ROTATE_RETRIES;
    rotate_retry_timer.reset();
    stepping_down = true;
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
