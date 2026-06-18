# analysis/figures/figures.py
"""Figuras de publicação (PNG rascunho + PDF vetorial)."""
import os
import math
import logging
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from analysis import metrics

log = logging.getLogger(__name__)

_POL_ORDER = ["round_robin", "energy", "energy_cooldown"]
_POL_LABEL = {"round_robin": "Round-Robin", "energy": "Energia",
              "energy_cooldown": "Energia+Cooldown"}

def _save(fig, outdir, name) -> str:
    os.makedirs(outdir, exist_ok=True)
    png = os.path.join(outdir, name + ".png")
    fig.savefig(png, dpi=150, bbox_inches="tight")
    fig.savefig(os.path.join(outdir, name + ".pdf"), bbox_inches="tight")
    plt.close(fig)
    return png

_ROLE_LABEL = {"leader": "Líder", "member": "Membro"}
_ROLE_ORDER = ["leader", "member"]

def _valid_num(x) -> bool:
    return x is not None and not (isinstance(x, float) and math.isnan(x))

def fig_energy_by_role(df, outdir, idle_ma=None) -> str | None:
    if not df["current_ma"].notna().any():
        log.warning("Figura A requer perfil calibrated (correntes reais); pulada.")
        return None
    d = metrics.energy_by_role(df).set_index("role")
    roles = [r for r in _ROLE_ORDER if r in d.index]
    labels = [_ROLE_LABEL[r] for r in roles]
    values = [d.loc[r, "current_ma_mean"] for r in roles]
    colors = ["#3b6ea5"] * len(roles)
    if _valid_num(idle_ma):
        labels.append("Idle (baseline)")
        values.append(idle_ma)
        colors.append("#a9c2dd")
    fig, ax = plt.subplots(figsize=(5, 3.2))
    ax.bar(labels, values, color=colors)
    ax.set_ylabel("Corrente média (mA)"); ax.set_xlabel("Papel")
    ax.set_title("A — Consumo por papel")
    return _save(fig, outdir, "fig_A_energia_por_papel")

def fig_fnd_by_policy(df, outdir) -> str:
    d = metrics.fnd_by_policy(df).set_index("policy").reindex(_POL_ORDER).dropna().reset_index()
    fig, ax = plt.subplots(figsize=(5, 3.2))
    ax.bar([_POL_LABEL[p] for p in d["policy"]], d["fnd_ms_mean"] / 1000.0,
           yerr=d["fnd_ms_std"] / 1000.0, capsize=4, color="#3b6ea5")
    ax.set_ylabel("FND (s)"); ax.set_title("B — First Node Death por política")
    return _save(fig, outdir, "fig_B_fnd_por_politica")

def fig_leadership_balance(df, outdir) -> str:
    d = metrics.leadership_balance(df)
    std = metrics.leadership_std(df).set_index("policy")
    fig, ax = plt.subplots(figsize=(6, 3.2))
    pols = [p for p in _POL_ORDER if p in set(d["policy"])]
    nodes = sorted(set(d["node_id"]))
    width = 0.8 / max(len(nodes), 1)
    for i, node in enumerate(nodes):
        sub = d[d["node_id"] == node].set_index("policy").reindex(pols)
        xs = [x + i * width for x in range(len(pols))]
        ax.bar(xs, sub["terms"].values, width=width, label=node)
    ax.set_xticks([x + 0.4 - width/2 for x in range(len(pols))])
    ax.set_xticklabels([f"{_POL_LABEL[p]}\nσ={std.loc[p,'std']:.2f}" for p in pols])
    ax.set_ylabel("Mandatos (média)"); ax.set_title("C — Balanceamento da liderança")
    ax.legend(title="Nó", fontsize=8)
    return _save(fig, outdir, "fig_C_balanceamento_lideranca")

def fig_residual_at_fnd(df, outdir) -> str:
    d = metrics.residual_at_fnd(df)
    spread = metrics.residual_spread(df).set_index("policy")
    fig, ax = plt.subplots(figsize=(6, 3.2))
    pols = [p for p in _POL_ORDER if p in set(d["policy"])]
    nodes = sorted(set(d["node_id"]))
    width = 0.8 / max(len(nodes), 1)
    for i, node in enumerate(nodes):
        sub = d[d["node_id"] == node].set_index("policy").reindex(pols)
        xs = [x + i * width for x in range(len(pols))]
        ax.bar(xs, sub["residual"].values, width=width, label=node)
    ax.set_xticks([x + 0.4 - width/2 for x in range(len(pols))])
    ax.set_xticklabels([f"{_POL_LABEL[p]}\nspread={spread.loc[p,'spread']:.0f}" for p in pols])
    ax.set_ylabel("Energia residual no FND"); ax.set_title("D — Carga residual desperdiçada")
    ax.legend(title="Nó", fontsize=8)
    return _save(fig, outdir, "fig_D_residual_no_fnd")

def fig_depletion_curves(df, outdir) -> str:
    d = metrics.depletion_curves(df)
    pols = [p for p in _POL_ORDER if p in set(d["policy"])]
    fig, axes = plt.subplots(1, len(pols), figsize=(4 * len(pols), 3.2), sharey=True)
    if len(pols) == 1:
        axes = [axes]
    for ax, pol in zip(axes, pols):
        sub = d[d["policy"] == pol]
        one_run = sub[sub["run_id"] == sorted(set(sub["run_id"]))[0]]
        for node, g in one_run.groupby("node_id"):
            g = g.sort_values("t_ms")
            ax.plot(g["t_ms"] / 1000.0, g["residual_pct"], label=node)
        ax.set_title(_POL_LABEL[pol]); ax.set_xlabel("Tempo (s)")
    axes[0].set_ylabel("Energia residual (%)")
    axes[-1].legend(title="Nó", fontsize=8)
    fig.suptitle("E — Curvas de depleção por nó")
    return _save(fig, outdir, "fig_E_curvas_deplecao")

def generate_all(df, outdir, idle_ma=None) -> list[str]:
    paths = [
        fig_energy_by_role(df, outdir, idle_ma),
        fig_fnd_by_policy(df, outdir),
        fig_leadership_balance(df, outdir),
        fig_residual_at_fnd(df, outdir),
        fig_depletion_curves(df, outdir),
    ]
    return [p for p in paths if p is not None]
