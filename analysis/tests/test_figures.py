# analysis/tests/test_figures.py
import os, pandas as pd
import matplotlib
matplotlib.use("Agg")  # headless
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
from analysis import figures, contract

def _df():
    # capacidade pequena (0.5 mAh) força o FND em ~130 s, exercitando B e D rápido
    # e mantendo a corrente por papel (métrica A) no perfil calibrado.
    cal = calibrated(leader_ma=120, member_ma=25, idle_ma=8, capacity_mah=0.5)
    frames = []
    for pol in ("round_robin","energy","energy_cooldown"):
        for s in (1,2):
            frames.append(sim.run_frame(3, pol, cal, s, max_ms=600_000))
    return pd.concat(frames, ignore_index=True)

def _abstract_like_df():
    # df minimo no contrato com correntes NaN (perfil abstract), sem rodar a sim.
    rows = [dict(run_id="r", source="sim", policy="round_robin", cluster_size=3,
                 node_id="n0", t_ms=0, event="sample", role="leader",
                 current_ma=float("nan"), power_mw=float("nan"),
                 residual=100.0, residual_pct=100.0),
            dict(run_id="r", source="sim", policy="round_robin", cluster_size=3,
                 node_id="n1", t_ms=0, event="sample", role="member",
                 current_ma=float("nan"), power_mw=float("nan"),
                 residual=100.0, residual_pct=100.0)]
    return contract.to_frame(rows)

def test_fig_a_none_without_real_currents(tmp_path):
    assert figures.fig_energy_by_role(_abstract_like_df(), str(tmp_path)) is None

def test_fig_a_writes_file_with_idle(tmp_path):
    path = figures.fig_energy_by_role(_df(), str(tmp_path), idle_ma=8.0)
    assert path is not None and os.path.exists(path) and os.path.getsize(path) > 0

def test_generate_all_calibrated_returns_five_with_idle(tmp_path):
    paths = figures.generate_all(_df(), str(tmp_path), idle_ma=8.0)
    assert len(paths) == 5
    for p in paths:
        assert os.path.exists(p) and os.path.getsize(p) > 0

def test_fig_f_tradeoff_writes_file(tmp_path):
    from analysis import run
    df = run.hetero_frames(seeds=(1,))
    path = figures.fig_cooldown_tradeoff(df, str(tmp_path))
    assert path is not None and os.path.exists(path) and os.path.getsize(path) > 0
