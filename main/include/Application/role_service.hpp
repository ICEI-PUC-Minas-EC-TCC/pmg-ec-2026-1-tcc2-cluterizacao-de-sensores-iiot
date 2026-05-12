#pragma once

#include "Network/network_controller.hpp"
#include <cstdint>

namespace service::application::role {

enum class Role : uint8_t {
    UNDECIDED = 0,
    LEADER,
    MEMBER,
};

void init();
void handler();

Role get_role();
bool is_leader();
controller::network::MacAddr get_leader_mac();
void on_peer_discovered();
void on_rotate_received(controller::network::MacAddr next_leader);

// Local view of the leader to advertise to peers in PING broadcasts:
// all-zero when UNDECIDED, own MAC when LEADER, leader_mac when MEMBER.
controller::network::MacAddr get_announced_leader();

// Reconcile local role with an announcement carried by a peer's PING.
// Recovers from lost ROTATE: a node that missed the rotation will adopt
// the announced leader within one PING period.
void on_leader_announced(controller::network::MacAddr announced_leader);

} // namespace service::application::role
