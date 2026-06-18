import os
import matplotlib; matplotlib.use("Agg")
from analysis import run

def test_generate_abstract_skips_fig_a(tmp_path):
    # No perfil abstract (correntes NaN), a Figura A e pulada: 4 figuras.
    paths = run.generate(str(tmp_path), profile_name="abstract", seeds=(1, 2))
    assert len(paths) == 4
    assert all(os.path.exists(p) for p in paths)
