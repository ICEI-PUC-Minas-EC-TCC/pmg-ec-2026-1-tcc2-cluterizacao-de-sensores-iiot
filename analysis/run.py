# analysis/run.py
"""Ponta a ponta: roda as 3 políticas × seeds e gera as figuras A–E."""
import argparse
import pandas as pd
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
from analysis import figures

POLICIES = ("round_robin", "energy", "energy_cooldown")

# Cenario heterogeneo: capacidades de bateria distintas por no (razao 10:7:4).
# Escala em mAh escolhida p/ FND em ~dezenas de mandatos (rapido); correntes
# placeholder ate a calibracao real (item 2). Ver spec cooldown-heterogeneo.
HETERO_CAPACITIES = (10.0, 7.0, 4.0)

def hetero_frames(seeds=(1, 2, 3, 4, 5), capacities=HETERO_CAPACITIES,
                  leader_ma=120.0, member_ma=25.0, idle_ma=8.0):
    """energy vs energy_cooldown num cluster HETEROGENEO (capacidades distintas
    por no). Retorna o DataFrame no contrato unico (sim). E onde a vantagem de
    balanceamento do cooldown se manifesta: o energy puro favorece o no de maior
    capacidade (mais residual), o cooldown forca a vez aos demais."""
    profiles = [calibrated(leader_ma, member_ma, idle_ma, capacity_mah=c) for c in capacities]
    frames = []
    for pol in ("energy", "energy_cooldown"):
        for s in seeds:
            frames.append(sim.run_frame(len(capacities), pol, profiles[0], s, profiles=profiles))
    return pd.concat(frames, ignore_index=True)

def generate_hetero(outdir, seeds=(1, 2, 3, 4, 5)):
    """Gera a figura F (trade-off justica vs durabilidade) no cluster heterogeneo."""
    df = hetero_frames(seeds=seeds)
    return [figures.fig_cooldown_tradeoff(df, outdir)]

def generate(outdir, profile_name="abstract", cluster_size=3, seeds=(1,2,3,4,5),
             leader_ma=120.0, member_ma=25.0, idle_ma=8.0):
    profile = ABSTRACT if profile_name == "abstract" else calibrated(leader_ma, member_ma, idle_ma)
    frames = []
    for pol in POLICIES:
        for s in seeds:
            frames.append(sim.run_frame(cluster_size, pol, profile, s))
    df = pd.concat(frames, ignore_index=True)
    return figures.generate_all(df, outdir, idle_ma=profile.current_idle_ma)

def main():
    ap = argparse.ArgumentParser(description="Gera as figuras A–E da Seção 5.2")
    ap.add_argument("--out", default="analysis/out")
    ap.add_argument("--profile", choices=["abstract", "calibrated"], default="abstract")
    ap.add_argument("--cluster-size", type=int, default=3)
    ap.add_argument("--seeds", type=int, nargs="+", default=[1,2,3,4,5])
    ap.add_argument("--leader-ma", type=float, default=120.0)
    ap.add_argument("--member-ma", type=float, default=25.0)
    ap.add_argument("--idle-ma", type=float, default=8.0)
    ap.add_argument("--hetero", action="store_true",
                    help="Gera a figura F (trade-off do cooldown em cluster heterogeneo)")
    a = ap.parse_args()
    if a.hetero:
        paths = generate_hetero(a.out, tuple(a.seeds))
    else:
        paths = generate(a.out, a.profile, a.cluster_size, tuple(a.seeds),
                         a.leader_ma, a.member_ma, a.idle_ma)
    for p in paths:
        print(p)

if __name__ == "__main__":
    main()
