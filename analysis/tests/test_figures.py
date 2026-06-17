# analysis/tests/test_figures.py
import os, pandas as pd
import matplotlib
matplotlib.use("Agg")  # headless
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
from analysis import figures

def _df():
    # capacidade pequena (0.5 mAh) força o FND em ~130 s, exercitando B e D rápido
    # e mantendo a corrente por papel (métrica A) no perfil calibrado.
    cal = calibrated(leader_ma=120, member_ma=25, idle_ma=8, capacity_mah=0.5)
    frames = []
    for pol in ("round_robin","energy","energy_cooldown"):
        for s in (1,2):
            frames.append(sim.run_frame(3, pol, cal, s, max_ms=600_000))
    return pd.concat(frames, ignore_index=True)

def test_generate_all_writes_five_pngs(tmp_path):
    paths = figures.generate_all(_df(), str(tmp_path))
    assert len(paths) == 5
    for p in paths:
        assert os.path.exists(p) and os.path.getsize(p) > 0
