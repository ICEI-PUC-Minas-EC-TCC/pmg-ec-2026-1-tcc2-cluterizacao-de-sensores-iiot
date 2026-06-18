# analysis/tests/test_sim.py
import pytest
from analysis.simulator import sim
from analysis.simulator.energy import ABSTRACT, calibrated
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

def test_run_aplica_profile_por_no():
    # Heterogeneidade: cada no recebe seu profile. O residual_pct deve ser
    # relativo a capacidade DO NO, nao a um profile global unico — entao dois
    # nos com capacidades diferentes comecam ambos perto de 100% da propria.
    grande = calibrated(120, 25, 8, capacity_mah=1.0)
    pequeno = calibrated(120, 25, 8, capacity_mah=0.5)
    rows = sim.run(cluster_size=2, strategy="round_robin", profile=grande,
                   seed=1, profiles=[grande, pequeno], max_ms=300, stop_at_fnd=False)
    df = contract.to_frame(rows)
    primeiros = df[df["event"] == "sample"].sort_values("t_ms").groupby("node_id").head(1)
    assert len(primeiros) == 2
    assert (primeiros["residual_pct"] > 95).all()

def test_run_rejeita_profiles_de_tamanho_errado():
    grande = calibrated(120, 25, 8, capacity_mah=1.0)
    with pytest.raises((ValueError, AssertionError)):
        sim.run(cluster_size=3, strategy="round_robin", profile=grande,
                seed=1, profiles=[grande, grande], max_ms=300)
