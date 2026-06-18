# analysis/tests/test_metrics.py
import pandas as pd
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
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

def test_leadership_std_counts_zero_leader_nodes():
    # n0 lidera 4x, n1 2x, n2 nunca (0). O desvio deve considerar os 3 nós:
    # std([4,2,0]) = 2.0; se n2 fosse ignorado, std([4,2]) ~ 1.41.
    def row(node, ev):
        return dict(run_id="r", source="sim", policy="energy", cluster_size=3,
                    node_id=node, t_ms=0, event=ev, role="leader",
                    current_ma=float("nan"), power_mw=float("nan"),
                    residual=1.0, residual_pct=1.0)
    rows = [row("n0", "became_leader")] * 4 + [row("n1", "became_leader")] * 2 + [row("n2", "sample")]
    df = pd.DataFrame(rows)
    std = metrics.leadership_std(df).set_index("policy")["std"]["energy"]
    assert abs(std - 2.0) < 1e-9

def test_energy_by_role_excludes_undecided():
    # No calibrado, lider e membro tem corrente constante por papel; undecided
    # (boot) usa idle_ma e NAO deve aparecer na metrica de consumo operacional.
    cal = calibrated(leader_ma=120, member_ma=25, idle_ma=8, capacity_mah=0.5)
    df = sim.run_frame(3, "round_robin", cal, seed=1, max_ms=120_000)
    out = metrics.energy_by_role(df).set_index("role")["current_ma_mean"]
    assert set(out.index) == {"leader", "member"}
    assert out["leader"] > out["member"]
    assert abs(out["leader"] - 120.0) < 1e-9
    assert abs(out["member"] - 25.0) < 1e-9
