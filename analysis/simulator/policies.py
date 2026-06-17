"""Port linha a linha de main/src/Application/leader_policy.cpp."""
from analysis import params

ZERO_MAC = bytes(6)

class CooldownTracker:
    """Espelha last_leadership + in_cooldown (leader_policy.cpp:43-51)."""
    def __init__(self):
        self._last: dict[bytes, int] = {}
    def on_became_leader(self, mac: bytes, now_ms: int) -> None:
        self._last[mac] = now_ms
    def in_cooldown(self, mac: bytes, now_ms: int) -> bool:
        t = self._last.get(mac)
        if t is None:
            return False
        return (now_ms - t) < params.COOLDOWN_MS

def pick_round_robin(cluster: list[bytes], current_leader: bytes) -> bytes:
    # leader_policy.cpp:69-77 — próximo no anel ordenado por MAC.
    try:
        idx = cluster.index(current_leader)
    except ValueError:
        idx = 0
    return cluster[(idx + 1) % len(cluster)]

def pick_energy(cluster, current_leader, own_mac, residual_of,
                in_cooldown, exclude_in_cooldown) -> bytes:
    # leader_policy.cpp:84-126.
    best = None
    best_energy = 0
    for mac in cluster:
        if mac == current_leader:
            continue
        if exclude_in_cooldown and in_cooldown(mac):
            continue
        energy, valid = residual_of(mac)
        if not valid:
            continue
        if best is None or energy > best_energy:
            best, best_energy = mac, energy
    if best is None:
        return pick_round_robin(cluster, current_leader)
    return best

def pick_next_leader(strategy, cluster, current_leader, own_mac,
                     residual_of, cooldown, now_ms) -> bytes:
    # leader_policy.cpp:128-142.
    if not cluster:
        return current_leader
    if strategy == "energy":
        return pick_energy(cluster, current_leader, own_mac, residual_of,
                           lambda m: cooldown.in_cooldown(m, now_ms),
                           exclude_in_cooldown=False)
    if strategy == "energy_cooldown":
        return pick_energy(cluster, current_leader, own_mac, residual_of,
                           lambda m: cooldown.in_cooldown(m, now_ms),
                           exclude_in_cooldown=True)
    return pick_round_robin(cluster, current_leader)
