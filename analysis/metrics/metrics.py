# analysis/metrics/metrics.py
"""Métricas A–E sobre o contrato. Agnóstico à origem (sim ou hardware)."""
import pandas as pd

def _fnd_per_run(df: pd.DataFrame) -> pd.DataFrame:
    deaths = df[df["event"] == "node_death"]
    grp = deaths.groupby(["policy", "run_id"])["t_ms"].min().reset_index()
    return grp.rename(columns={"t_ms": "fnd_ms"})

# A
def energy_by_role(df: pd.DataFrame) -> pd.DataFrame:
    s = df[df["event"] == "sample"].dropna(subset=["current_ma"])
    out = s.groupby("role")["current_ma"].mean().reset_index()
    return out.rename(columns={"current_ma": "current_ma_mean"})

# B
def fnd_by_policy(df: pd.DataFrame) -> pd.DataFrame:
    per_run = _fnd_per_run(df)
    out = per_run.groupby("policy")["fnd_ms"].agg(["mean", "std"]).reset_index()
    return out.rename(columns={"mean": "fnd_ms_mean", "std": "fnd_ms_std"}).fillna(0.0)

# C
def leadership_balance(df: pd.DataFrame) -> pd.DataFrame:
    bl = df[df["event"] == "became_leader"]
    terms = bl.groupby(["policy", "run_id", "node_id"]).size().reset_index(name="terms")
    tot = terms.groupby(["policy", "run_id"])["terms"].transform("sum")
    terms["share"] = terms["terms"] / tot
    return terms.groupby(["policy", "node_id"])[["terms", "share"]].mean().reset_index()

def leadership_std(df: pd.DataFrame) -> pd.DataFrame:
    bl = df[df["event"] == "became_leader"]
    terms = bl.groupby(["policy", "run_id", "node_id"]).size().reset_index(name="terms")
    std = terms.groupby(["policy", "run_id"])["terms"].std().reset_index()
    return std.groupby("policy")["terms"].mean().reset_index().rename(columns={"terms": "std"}).fillna(0.0)

# D
def residual_at_fnd(df: pd.DataFrame) -> pd.DataFrame:
    per_run = _fnd_per_run(df).set_index(["policy", "run_id"])["fnd_ms"]
    samples = df[df["event"] == "sample"]
    out = []
    for (policy, run_id), g in samples.groupby(["policy", "run_id"]):
        if (policy, run_id) not in per_run.index:
            continue
        fnd = per_run.loc[(policy, run_id)]
        upto = g[g["t_ms"] <= fnd]
        last = upto.sort_values("t_ms").groupby("node_id").tail(1)
        for _, r in last.iterrows():
            out.append(dict(policy=policy, run_id=run_id,
                            node_id=r["node_id"], residual=r["residual"]))
    res = pd.DataFrame(out)
    return res.groupby(["policy", "node_id"])["residual"].mean().reset_index()

def residual_spread(df: pd.DataFrame) -> pd.DataFrame:
    per_run = _fnd_per_run(df).set_index(["policy", "run_id"])["fnd_ms"]
    samples = df[df["event"] == "sample"]
    rows = []
    for (policy, run_id), g in samples.groupby(["policy", "run_id"]):
        if (policy, run_id) not in per_run.index:
            continue
        fnd = per_run.loc[(policy, run_id)]
        upto = g[g["t_ms"] <= fnd]
        last = upto.sort_values("t_ms").groupby("node_id").tail(1)
        survivors = last[last["residual"] > 0]["residual"]
        rows.append(dict(policy=policy, run_id=run_id, spread=survivors.std()))
    res = pd.DataFrame(rows)
    return res.groupby("policy")["spread"].mean().reset_index().fillna(0.0)

# E
def depletion_curves(df: pd.DataFrame) -> pd.DataFrame:
    s = df[df["event"] == "sample"]
    return s[["policy", "run_id", "node_id", "t_ms", "residual_pct"]].copy()
