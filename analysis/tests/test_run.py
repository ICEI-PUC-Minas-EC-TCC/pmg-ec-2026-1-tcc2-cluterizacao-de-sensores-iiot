import os
import matplotlib; matplotlib.use("Agg")
from analysis import run

def test_generate_abstract_skips_fig_a(tmp_path):
    # No perfil abstract (correntes NaN), a Figura A é pulada: 4 figuras.
    paths = run.generate(str(tmp_path), profile_name="abstract", seeds=(1, 2))
    assert len(paths) == 4
    assert all(os.path.exists(p) for p in paths)

def test_generate_hetero_writes_tradeoff_fig(tmp_path):
    # Cenario heterogeneo gera a figura F do trade-off do cooldown.
    paths = run.generate_hetero(str(tmp_path), seeds=(1,))
    assert len(paths) == 1 and all(os.path.exists(p) for p in paths)
