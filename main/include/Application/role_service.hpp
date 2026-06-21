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
// Morte simulada: na bancada a fonte mantem o no' ligado mesmo com a
// bateria medida zerada. Ao "morrer" (battery_pct <= limiar) o no' para de
// participar: nao publica, nao envia, recusa lideranca. Latch ate' o reset.
void mark_dead();
bool is_dead();
controller::network::MacAddr get_leader_mac();
void on_rotate_received(controller::network::MacAddr next_leader);

// Local view of the leader to advertise to peers in PING broadcasts:
// all-zero when UNDECIDED, own MAC when LEADER, leader_mac when MEMBER.
controller::network::MacAddr get_announced_leader();

// Reconcile local role with an announcement carried by a peer's PING.
// Recovers from lost ROTATE: a node that missed the rotation will adopt
// the announced leader within one PING period. `sender` is the MAC that
// transmitted the PING; it lets a leader detect a competing leader
// (announced == sender) and resolve split-brain deterministically.
void on_leader_announced(controller::network::MacAddr announced_leader,
                         controller::network::MacAddr sender);

// Posicao (0-based) do proprio MAC no anel de MACs ordenado do cluster, e o
// tamanho do cluster. Usado pelo reset escalonado.
int own_rank();

} // namespace service::application::role
