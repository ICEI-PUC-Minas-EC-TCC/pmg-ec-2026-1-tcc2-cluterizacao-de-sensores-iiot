#pragma once

#include "Network/network_controller.hpp"
#include <cstdint>

namespace service::application::energy {

// Simulated energy budget. Units are abstract (the policy only cares about
// relative ordering, not absolute units). When the ADC integration arrives,
// get_residual() and the local accounting switch to a real measurement;
// peer tracking via ping piggyback stays the same.

void init();

// Local accounting: per-operation hooks called from the spots where the
// corresponding work happens.
void on_mqtt_publish();
void on_espnow_send();

// Periodic decay (idle cost), called at a fixed cadence from the main loop.
void tick();

uint32_t get_residual();

// Reset de bancada: grava o residual-alvo (pct de INITIAL_BUDGET) no NVS.
// Usado pelo fluxo de reset antes do restart.
void persist_reset_pct(float pct);

// Peer table updated from PingPayload.residual_energy. Returns 0 (and a
// false `valid` out-flag) if no fresh sample is available for that MAC.
void on_peer_energy(controller::network::MacAddr peer, uint32_t residual);
uint32_t get_peer_residual(controller::network::MacAddr peer, bool *valid = nullptr);

} // namespace service::application::energy
