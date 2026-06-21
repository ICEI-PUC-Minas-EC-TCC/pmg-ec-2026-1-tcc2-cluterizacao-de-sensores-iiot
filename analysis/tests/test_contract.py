import pandas as pd
import pytest
from analysis import contract

def test_to_frame_has_all_columns():
    df = contract.to_frame([])
    assert list(df.columns) == contract.COLUMNS

def test_validate_accepts_minimal_row():
    rows = [dict(run_id="r", source="sim", policy="round_robin", cluster_size=3,
                 node_id="n0", t_ms=0, event="sample", role="leader",
                 current_ma=float("nan"), power_mw=float("nan"),
                 residual=100000.0, residual_pct=100.0)]
    contract.validate(contract.to_frame(rows))  # não levanta

def test_validate_rejects_bad_event():
    rows = [dict(run_id="r", source="sim", policy="round_robin", cluster_size=3,
                 node_id="n0", t_ms=0, event="explode", role="leader",
                 current_ma=float("nan"), power_mw=float("nan"),
                 residual=1.0, residual_pct=1.0)]
    with pytest.raises(ValueError):
        contract.validate(contract.to_frame(rows))
