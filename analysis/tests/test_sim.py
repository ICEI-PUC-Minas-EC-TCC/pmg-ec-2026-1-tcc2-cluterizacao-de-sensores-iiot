# analysis/tests/test_sim.py
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT
from analysis import contract

def test_run_emits_valid_contract():
    rows = sim.run(cluster_size=3, strategy="round_robin", profile=ABSTRACT,
                   seed=1, max_ms=120_000)
    df = contract.to_frame(rows)
    contract.validate(df)
    assert (df["event"] == "became_leader").sum() >= 1
    assert (df["event"] == "sample").sum() >= 3

def test_run_reaches_fnd():
    rows = sim.run(cluster_size=3, strategy="round_robin", profile=ABSTRACT, seed=1)
    deaths = [r for r in rows if r["event"] == "node_death"]
    assert len(deaths) >= 1  # FND atingido

def test_runs_are_deterministic_per_seed():
    a = sim.run_frame(cluster_size=3, strategy="energy", profile=ABSTRACT, seed=7, max_ms=60_000)
    b = sim.run_frame(cluster_size=3, strategy="energy", profile=ABSTRACT, seed=7, max_ms=60_000)
    assert a.equals(b)
