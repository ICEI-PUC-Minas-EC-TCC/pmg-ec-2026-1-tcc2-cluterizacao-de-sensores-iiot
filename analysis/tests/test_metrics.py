# analysis/tests/test_metrics.py
import pandas as pd
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT
from analysis import metrics

def many(strategy, seeds=(1,2,3)):
    frames = [sim.run_frame(3, strategy, ABSTRACT, s) for s in seeds]
    return pd.concat(frames, ignore_index=True)

def test_fnd_by_policy_one_row_per_policy():
    df = pd.concat([many("round_robin"), many("energy")], ignore_index=True)
    out = metrics.fnd_by_policy(df)
    assert set(out["policy"]) == {"round_robin", "energy"}
    assert (out["fnd_ms_mean"] > 0).all()

def test_cooldown_balances_better_than_energy():
    # Efeito que o artigo afirma: cooldown reduz o desvio-padrao da lideranca.
    df = pd.concat([many("energy"), many("energy_cooldown")], ignore_index=True)
    std = metrics.leadership_std(df).set_index("policy")["std"]
    assert std["energy_cooldown"] <= std["energy"]

def test_depletion_curves_have_time_and_pct():
    df = many("round_robin", seeds=(1,))
    out = metrics.depletion_curves(df)
    assert {"policy","node_id","t_ms","residual_pct"} <= set(out.columns)
    assert out["residual_pct"].max() <= 100.0
