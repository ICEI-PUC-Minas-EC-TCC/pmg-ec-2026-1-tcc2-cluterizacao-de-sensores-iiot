"""Lê o SQLite do servidor MQTT (tabela leituras) e mapeia para o contrato.
Nesta etapa cobre a métrica E (residual_pct = bateria_pct). Papel/política/eventos
reais entram no Spec 2 (extensão de firmware)."""
import sqlite3
import pandas as pd
from analysis import contract

def load(db_path: str, policy: str = "unknown") -> pd.DataFrame:
    conn = sqlite3.connect(db_path)
    raw = pd.read_sql_query(
        "SELECT topico, corrente_ma, bateria_pct, timestamp FROM leituras ORDER BY timestamp, id",
        conn)
    conn.close()
    if raw.empty:
        return pd.DataFrame(columns=contract.COLUMNS)
    ts = pd.to_datetime(raw["timestamp"])
    t0 = ts.min()
    t_ms = ((ts - t0).dt.total_seconds() * 1000).astype("int64")
    n = len(raw)
    df = pd.DataFrame({
        "run_id": [f"hw-{db_path}"] * n,
        "source": ["hw"] * n,
        "policy": [policy] * n,
        "cluster_size": [raw["topico"].nunique()] * n,
        "node_id": raw["topico"],
        "t_ms": t_ms,
        "event": ["sample"] * n,
        "role": [None] * n,
        "current_ma": raw["corrente_ma"].astype(float),
        "power_mw": [float("nan")] * n,
        "residual": raw["bateria_pct"].astype(float),
        "residual_pct": raw["bateria_pct"].astype(float),
    })
    return df[contract.COLUMNS]
