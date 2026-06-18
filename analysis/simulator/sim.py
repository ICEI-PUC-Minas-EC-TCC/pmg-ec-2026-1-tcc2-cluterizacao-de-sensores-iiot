# analysis/simulator/sim.py
"""Motor de simulação em passo fixo de 100 ms (espelha o loop do firmware)."""
import random
from analysis import params, contract
from analysis.simulator.nodes import Node
from analysis.simulator.transport import Transport

def make_macs(n: int) -> list[bytes]:
    return [bytes([0, 0, 0, 0, 0, i + 1]) for i in range(n)]

def run(cluster_size, strategy, profile, seed, max_ms=5_000_000,
        sample_period_ms=params.PING_PERIOD_MS, stop_at_fnd=True) -> list[dict]:
    # max_ms é só um teto de segurança: com stop_at_fnd=True a execução para no
    # FND real (~3600 s no perfil abstract com N=3), então não roda até max_ms.
    rng = random.Random(seed)
    macs = make_macs(cluster_size)
    transport = Transport(rng, macs, ping_loss=params.PING_BROADCAST_LOSS,
                          unicast_loss=params.UNICAST_LOSS)
    run_id = f"{strategy}-{profile.name}-N{cluster_size}-s{seed}"
    mac_to_id = {m: f"n{i}" for i, m in enumerate(macs)}
    rows: list[dict] = []

    def base_row(node, now_ms, event):
        return dict(run_id=run_id, source="sim", policy=strategy,
                    cluster_size=cluster_size, node_id=mac_to_id[node.mac],
                    t_ms=now_ms, event=event, role=node.role,
                    current_ma=profile.current_for(node.role), power_mw=float("nan"),
                    residual=node.energy.residual, residual_pct=_pct(node, profile))

    def emit(node, now_ms, event):
        rows.append(base_row(node, now_ms, event))

    nodes = [Node(m, profile, strategy, transport, emit) for m in macs]

    t = 0
    last_sample = -sample_period_ms
    while t <= max_ms:
        for node in nodes:
            node.step(now_ms=t)
        if t - last_sample >= sample_period_ms:
            last_sample = t
            for node in nodes:
                if node.alive:
                    rows.append(base_row(node, t, "sample"))
        if stop_at_fnd and any(not n.alive for n in nodes):
            break
        t += params.LOOP_PERIOD_MS
    return rows

def _pct(node, profile) -> float:
    return 100.0 * node.energy.residual / profile.initial

def run_frame(*args, **kwargs):
    return contract.to_frame(run(*args, **kwargs))
