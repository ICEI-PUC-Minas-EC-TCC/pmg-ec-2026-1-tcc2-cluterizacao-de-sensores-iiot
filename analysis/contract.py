"""Contrato de dados único: schema do log longo emitido por sim e hardware."""
import pandas as pd

COLUMNS = [
    "run_id", "source", "policy", "cluster_size", "node_id", "t_ms",
    "event", "role", "current_ma", "power_mw", "residual", "residual_pct",
]
EVENTS = {"sample", "became_leader", "term_expired", "rotate_sent",
          "rotate_applied", "election", "node_death"}
ROLES = {"leader", "member", "undecided"}
POLICIES = {"round_robin", "energy", "energy_cooldown", "unknown"}

def empty_records() -> list[dict]:
    return []

def to_frame(records: list[dict]) -> pd.DataFrame:
    df = pd.DataFrame(records, columns=COLUMNS)
    return df

def validate(df: pd.DataFrame) -> None:
    missing = set(COLUMNS) - set(df.columns)
    if missing:
        raise ValueError(f"colunas faltando: {missing}")
    bad_events = set(df["event"].dropna()) - EVENTS
    if bad_events:
        raise ValueError(f"eventos inválidos: {bad_events}")
    bad_roles = set(df["role"].dropna()) - ROLES
    if bad_roles:
        raise ValueError(f"papéis inválidos: {bad_roles}")
    bad_pol = set(df["policy"].dropna()) - POLICIES
    if bad_pol:
        raise ValueError(f"políticas inválidas: {bad_pol}")
