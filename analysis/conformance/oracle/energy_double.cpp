// Implementa a API energy:: (declarada em energy_service.hpp) com valores
// controláveis, em vez de compilar energy_service.cpp (cujo residual local não
// é settável externamente). O objetivo é exercitar o leader_policy.cpp REAL.
#include "Application/energy_service.hpp"
#include <cstring>
#include <map>
#include <array>

using controller::network::MacAddr;
static std::map<std::array<uint8_t,6>, uint32_t> g_res;
static std::map<std::array<uint8_t,6>, bool> g_valid;
extern uint8_t g_oracle_own_mac[6];

static std::array<uint8_t,6> key(MacAddr m){ std::array<uint8_t,6> k; memcpy(k.data(), m.data(), 6); return k; }

namespace oracle_energy {
    void clear(){ g_res.clear(); g_valid.clear(); }
    void set(MacAddr m, uint32_t r){ g_res[key(m)] = r; g_valid[key(m)] = true; }
}

namespace service::application::energy {
    void init() {}
    void on_mqtt_publish() {}
    void on_espnow_send() {}
    void tick() {}
    uint32_t get_residual() {
        std::array<uint8_t,6> k; memcpy(k.data(), g_oracle_own_mac, 6);
        auto it = g_res.find(k); return it == g_res.end() ? 0 : it->second;
    }
    void on_peer_energy(MacAddr, uint32_t) {}
    uint32_t get_peer_residual(MacAddr peer, bool *valid) {
        auto it = g_res.find(key(peer));
        bool ok = it != g_res.end() && g_valid[key(peer)];
        if (valid) *valid = ok;
        return ok ? it->second : 0;
    }
}
