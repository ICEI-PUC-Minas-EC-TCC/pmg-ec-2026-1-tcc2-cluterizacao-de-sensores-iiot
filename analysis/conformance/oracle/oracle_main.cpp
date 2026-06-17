#include "Application/leader_policy.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using controller::network::MacAddr;
uint8_t g_oracle_own_mac[6] = {0};
int64_t g_oracle_time_us = 0;
namespace oracle_energy { void clear(); void set(MacAddr, uint32_t); }

static MacAddr parse_mac(const std::string &hex) {
    MacAddr m{};
    for (int i = 0; i < 6; ++i)
        m[i] = (uint8_t)std::stoi(hex.substr(i * 2, 2), nullptr, 16);
    return m;
}
static std::string mac_hex(const MacAddr &m) {
    char buf[13];
    for (int i = 0; i < 6; ++i) sprintf(buf + i * 2, "%02x", m[i]);
    return std::string(buf, 12);
}

int main() {
    namespace lp = service::application::leader_policy;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string own_s, cur_s, peers_s;
        std::getline(ss, own_s, ';');
        std::getline(ss, cur_s, ';');
        std::getline(ss, peers_s, ';');

        lp::init();
        oracle_energy::clear();
        g_oracle_time_us = 0;
        memcpy(g_oracle_own_mac, parse_mac(own_s).data(), 6);
        MacAddr current = parse_mac(cur_s);

        std::vector<MacAddr> cluster;
        std::stringstream ps(peers_s);
        std::string tok;
        while (std::getline(ps, tok, ',')) {
            if (tok.empty()) continue;
            auto a = tok.find(':'), b = tok.rfind(':');
            MacAddr mac = parse_mac(tok.substr(0, a));
            uint32_t res = (uint32_t)std::stoul(tok.substr(a + 1, b - a - 1));
            int cool = std::stoi(tok.substr(b + 1));
            cluster.push_back(mac);
            oracle_energy::set(mac, res);
            if (cool) lp::on_became_leader(mac);  // registra liderança recente em t=0
        }
        g_oracle_time_us = 1000LL * 1000;  // 1 s: < COOLDOWN_MS -> recentes seguem em cooldown
        std::sort(cluster.begin(), cluster.end(),
                  [](const MacAddr &x, const MacAddr &y){ return memcmp(x.data(), y.data(), 6) < 0; });

        MacAddr chosen = lp::pick_next_leader(cluster, current);
        std::cout << mac_hex(chosen) << "\n";
    }
    return 0;
}
