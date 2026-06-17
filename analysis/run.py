# analysis/run.py
"""Ponta a ponta: roda as 3 políticas × seeds e gera as figuras A–E."""
import argparse
import pandas as pd
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
from analysis import figures

POLICIES = ("round_robin", "energy", "energy_cooldown")

def generate(outdir, profile_name="abstract", cluster_size=3, seeds=(1,2,3,4,5),
             leader_ma=120.0, member_ma=25.0, idle_ma=8.0):
    profile = ABSTRACT if profile_name == "abstract" else calibrated(leader_ma, member_ma, idle_ma)
    frames = []
    for pol in POLICIES:
        for s in seeds:
            frames.append(sim.run_frame(cluster_size, pol, profile, s))
    df = pd.concat(frames, ignore_index=True)
    return figures.generate_all(df, outdir)

def main():
    ap = argparse.ArgumentParser(description="Gera as figuras A–E da Seção 5.2")
    ap.add_argument("--out", default="analysis/out")
    ap.add_argument("--profile", choices=["abstract", "calibrated"], default="abstract")
    ap.add_argument("--cluster-size", type=int, default=3)
    ap.add_argument("--seeds", type=int, nargs="+", default=[1,2,3,4,5])
    ap.add_argument("--leader-ma", type=float, default=120.0)
    ap.add_argument("--member-ma", type=float, default=25.0)
    ap.add_argument("--idle-ma", type=float, default=8.0)
    a = ap.parse_args()
    paths = generate(a.out, a.profile, a.cluster_size, tuple(a.seeds),
                     a.leader_ma, a.member_ma, a.idle_ma)
    for p in paths:
        print(p)

if __name__ == "__main__":
    main()
