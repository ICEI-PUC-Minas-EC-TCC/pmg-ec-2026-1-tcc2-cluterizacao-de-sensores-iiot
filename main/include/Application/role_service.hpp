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

} // namespace service::application::role
