# analysis/tests/test_run.py
import os
import matplotlib; matplotlib.use("Agg")
from analysis import run

def test_generate_end_to_end(tmp_path):
    paths = run.generate(str(tmp_path), profile_name="abstract", seeds=(1,2))
    assert len(paths) == 5
    assert all(os.path.exists(p) for p in paths)
