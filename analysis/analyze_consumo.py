#!/usr/bin/env python3
"""Analise de CONSUMO TOTAL dos 6 ensaios de 2026-06-20 (InfluxDB).

Motivacao: a comparacao por bateria/FND ficou inconclusiva (nos com energia
inicial diferente, nº de nos diferente, ensaios cortados). O consumo total e'
uma medicao end-to-end de energia, robusta a esses problemas: somando a
corrente de TODOS os nos a cada instante temos a potencia instantanea da rede;
integrando no tempo temos a carga total gasta (mAh) — que ja inclui radio,
trocas de lider e transientes num unico numero.

Metricas por ensaio:
  - I_total medio (mA)  = media no tempo da soma das correntes dos nos ativos.
  - Q total (mAh)       = integral de I_total no tempo (carga gasta pela rede).
  - I por no (mA/no)    = I_total / nº medio de nos ativos  -> metrica JUSTA.
  - Q por no (mAh/no)   = Q_total / nº de nos               -> compara politicas.

Como RR/EN tem 5 nos e EC tem 4, o headline da comparacao e' POR NO.

Requer rede para 64.181.160.152:8086 (rode fora do sandbox).
"""
from influxdb_client import InfluxDBClient
from pathlib import Path
import collections
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ORG, BUCKET = "meu_tcc", "dados_esps"
URL = "http://64.181.160.152:8086"
TOKEN = Path(__file__).resolve().parents[1].joinpath("server/.influx_token").read_text().strip()
OUTDIR = Path(__file__).resolve().parent / "figures" / "ensaios_2026-06-20"
OUTDIR.mkdir(parents=True, exist_ok=True)

SKIP_S = 60        # descarta transiente de reset/descoberta
BUCKET_S = 10      # grade comum p/ somar as correntes dos nos (cadencia bruta ~1-2s)

# (rotulo, cenario, policy, run_id) — mesmos run_id da analise de bateria.
ENSAIOS = [
    ("round_robin",     "A", "round_robin",     "31/12/1969 21:03:58"),
    ("round_robin",     "B", "round_robin",     "20/06/2026 10:59:21"),
    ("energy",          "A", "energy",          "31/12/1969 21:00:17"),
    ("energy",          "B", "energy",          "20/06/2026 12:04:05"),
    ("energy_cooldown", "A", "energy_cooldown", "31/12/1969 21:00:52"),
    ("energy_cooldown", "B", "energy_cooldown", "20/06/2026 13:00:03"),
]
COLORS = {"round_robin": "#1f77b4", "energy": "#d62728", "energy_cooldown": "#2ca02c"}
ABBR = {"round_robin": "RR", "energy": "EN", "energy_cooldown": "EC"}

cli = InfluxDBClient(url=URL, token=TOKEN, org=ORG, timeout=60000)
qapi = cli.query_api()


def fetch_current(policy, run):
    """Retorna {node: [(elapsed_s, current_ma)]} ordenado, e t0."""
    flux = f'''
from(bucket:"{BUCKET}")
 |> range(start: 2026-06-20T00:00:00Z, stop: 2026-06-21T00:00:00Z)
 |> filter(fn:(r)=>r._measurement=="consumo" and r._field=="current_ma"
           and r.policy=="{policy}" and r.run=="{run}")
 |> keep(columns:["_time","_value","node"])
'''
    rows = []
    for t in qapi.query(flux):
        for r in t.records:
            rows.append((r.get_time(), r.get_value(), r.values.get("node")))
    if not rows:
        return {}, None
    rows.sort(key=lambda x: x[0])
    t0 = rows[0][0]
    by_node = collections.defaultdict(list)
    for tm, val, node in rows:
        if val is None:
            continue
        by_node[node].append(((tm - t0).total_seconds(), val))
    return by_node, t0


def node_grid(by_node):
    """Reamostra cada no numa grade comum de BUCKET_S.

    Retorna (node_buckets, kmin, kmax), onde node_buckets[node][k] e' a corrente
    media (mA) do no no bucket k. Buckets sem amostra DENTRO da vida do no
    [1o bucket, ultimo bucket] sao preenchidos com o ultimo valor conhecido
    (forward-fill) — evita picos falsos quando um no apenas atrasa um report.
    Fora desse intervalo o no nao existe (None).
    """
    raw = {}
    for node, series in by_node.items():
        per = collections.defaultdict(list)
        for el, val in series:
            if el < SKIP_S:
                continue
            per[int(el // BUCKET_S)].append(val)
        if per:
            raw[node] = {k: sum(v) / len(v) for k, v in per.items()}
    if not raw:
        return {}, 0, -1
    kmin = min(min(b) for b in raw.values())
    kmax = max(max(b) for b in raw.values())
    filled = {}
    for node, b in raw.items():
        lo, hi = min(b), max(b)
        last = None
        f = {}
        for k in range(lo, hi + 1):
            if k in b:
                last = b[k]
            f[k] = last  # forward-fill dentro da vida do no
        filled[node] = f
    return filled, kmin, kmax


def network_series(grid):
    """Soma as correntes dos nos -> I_total(t).

    grid = (node_buckets, kmin, kmax). Um no fora da sua vida nao entra na soma
    (morte real). Retorna lista [(t_centro_s, I_total_mA, n_nos_ativos)].
    """
    node_buckets, kmin, kmax = grid
    out = []
    for k in range(kmin, kmax + 1):
        i_total, n_active = 0.0, 0
        for b in node_buckets.values():
            if k in b:
                i_total += b[k]
                n_active += 1
        if n_active:
            out.append((k * BUCKET_S + BUCKET_S / 2.0, i_total, n_active))
    return out


def cumulative(grid):
    """Soma ACUMULADA da corrente ao longo do tempo, em mA (nao mAh).

    Literal: comeca em 0 e a cada bucket soma o valor de corrente (mA) de cada
    no — tempo 0 = 0, depois +~108, +~108, ... Quando um no morre, a curva dele
    achata no patamar. NB: o valor absoluto depende do passo da grade (BUCKET_S
    = 10 s); e' uma quantidade visual/relativa. A energia fisica (mAh) esta na
    tabela. Retorna:
      ts   -> lista de tempos (min), comecando em 0
      pern -> {node: [soma_acumulada_mA por instante]}
      tot  -> [soma_acumulada_mA da rede inteira por instante]
    """
    node_buckets, kmin, kmax = grid
    ts = [0.0]
    pern = {n: [0.0] for n in node_buckets}
    tot = [0.0]
    for k in range(kmin, kmax + 1):
        ts.append((k - kmin + 1) * BUCKET_S / 60.0)  # min decorridos
        step_tot = 0.0
        for n, b in node_buckets.items():
            inc = b[k] if k in b else 0.0  # corrente (mA); nao soma apos a morte
            pern[n].append(pern[n][-1] + inc)
            step_tot += inc
        tot.append(tot[-1] + step_tot)
    return ts, pern, tot


def metrics(by_node, net):
    n_nodes = len(by_node)
    if not net:
        return None
    dt_h = BUCKET_S / 3600.0
    q_total = sum(i for _, i, _ in net) * dt_h            # mAh (integral)
    i_total_mean = sum(i for _, i, _ in net) / len(net)   # mA medio da rede
    n_active_mean = sum(n for _, _, n in net) / len(net)  # nº medio de nos ativos
    dur_min = (net[-1][0] - net[0][0]) / 60.0 + BUCKET_S / 60.0
    # por no (metrica justa): usa nº medio de nos ativos
    i_per_node = i_total_mean / n_active_mean if n_active_mean else 0
    q_per_node = q_total / n_nodes if n_nodes else 0
    return {
        "n_nodes": n_nodes, "n_active_mean": n_active_mean, "dur_min": dur_min,
        "q_total": q_total, "i_total_mean": i_total_mean,
        "i_per_node": i_per_node, "q_per_node": q_per_node,
    }


def main():
    summary = []  # (policy, cen, metrics, net, grid)
    for label, cen, policy, run in ENSAIOS:
        by_node, t0 = fetch_current(policy, run)
        grid = node_grid(by_node)
        net = network_series(grid)
        m = metrics(by_node, net)
        summary.append((policy, cen, m, net, grid))

        print("=" * 68)
        print(f"{policy} / {cen}  (run={run})")
        if not m:
            print("  (sem dados de corrente)")
            continue
        print(f"  nos={m['n_nodes']}  nos_ativos_med={m['n_active_mean']:.2f}  "
              f"dur={m['dur_min']:.1f}min")
        print(f"  I_total medio = {m['i_total_mean']:7.1f} mA   "
              f"(rede inteira, soma das correntes)")
        print(f"  Q_total       = {m['q_total']:7.1f} mAh  "
              f"(carga gasta pela rede no ensaio)")
        print(f"  I por no      = {m['i_per_node']:7.1f} mA/no  <-- comparacao justa")
        print(f"  Q por no      = {m['q_per_node']:7.1f} mAh/no")

    # ---------- tabela comparativa ----------
    print("\n" + "=" * 68)
    print("COMPARACAO DE CONSUMO TOTAL (somando as correntes)")
    print("-" * 68)
    hdr = f"{'ensaio':<18}{'nos':>4}{'I_tot(mA)':>11}{'Q(mAh)':>9}{'I/no(mA)':>10}{'Q/no(mAh)':>11}"
    print(hdr)
    for policy, cen, m, _, _ in summary:
        if not m:
            print(f"{ABBR[policy]+'-'+cen:<18}{'-':>4}{'sem dados':>11}")
            continue
        print(f"{ABBR[policy]+'-'+cen:<18}{m['n_nodes']:>4}{m['i_total_mean']:>11.1f}"
              f"{m['q_total']:>9.1f}{m['i_per_node']:>10.1f}{m['q_per_node']:>11.1f}")

    for cen in ("A", "B"):
        grp = [(p, m) for p, c, m, _, _ in summary if c == cen and m]
        if len(grp) >= 2:
            ipn = [m["i_per_node"] for _, m in grp]
            print(f"\n  Cenario {cen}: I por no = "
                  + ", ".join(f"{ABBR[p]} {m['i_per_node']:.1f}" for p, m in grp)
                  + f"  | amplitude {max(ipn)-min(ipn):.1f} mA "
                  + f"({100*(max(ipn)-min(ipn))/(sum(ipn)/len(ipn)):.1f}% da media)")

    # ---------- figura: I_total(t) sobreposto por cenario ----------
    for cen in ("A", "B"):
        plt.figure(figsize=(9, 4.5))
        any_pts = False
        for policy in ("round_robin", "energy", "energy_cooldown"):
            net = next((n for p, c, m, n, g in summary if p == policy and c == cen and m), None)
            if not net:
                continue
            xs = [t / 60.0 for t, _, _ in net]
            ys = [i for _, i, _ in net]
            plt.plot(xs, ys, label=policy, color=COLORS[policy], lw=1.3, alpha=0.85)
            any_pts = True
        if any_pts:
            plt.title(f"Consumo total da rede — Cenario {cen} "
                      f"({'CHEIO 100%' if cen == 'A' else 'ESCALONADO'})")
            plt.xlabel("tempo decorrido (min)")
            plt.ylabel("corrente total da rede (mA, soma dos nos)")
            plt.grid(alpha=0.3)
            plt.legend()
            plt.tight_layout()
            plt.savefig(OUTDIR / f"consumo_total_cenario_{cen}.png", dpi=110)
        plt.close()

    # ---------- figura: consumo ACUMULADO por no (um grafico por ensaio) ----------
    for policy, cen, m, net, grid in summary:
        if not m:
            continue
        ts, pern, tot = cumulative(grid)
        plt.figure(figsize=(9, 4.5))
        for node in sorted(pern):
            plt.plot(ts, pern[node], lw=1.5, label=f"{node[-4:]} ({pern[node][-1]:.0f} mA)")
        plt.title(f"{policy} — Cenario {cen}  (corrente acumulada por no)")
        plt.xlabel("tempo decorrido (min)")
        plt.ylabel("soma acumulada da corrente (mA)")
        plt.grid(alpha=0.3)
        plt.legend(fontsize=8, ncol=2)
        plt.tight_layout()
        plt.savefig(OUTDIR / f"consumo_acumulado_{policy}_{cen}.png", dpi=110)
        plt.close()

    # ---------- figura: consumo acumulado da REDE, 3 politicas por cenario ----------
    for cen in ("A", "B"):
        plt.figure(figsize=(9, 4.5))
        any_pts = False
        for policy in ("round_robin", "energy", "energy_cooldown"):
            grid = next((g for p, c, m, n, g in summary
                         if p == policy and c == cen and m), None)
            if not grid:
                continue
            ts, _, tot = cumulative(grid)
            plt.plot(ts, tot, label=f"{policy} ({tot[-1]:.0f} mA)",
                     color=COLORS[policy], lw=1.8)
            any_pts = True
        if any_pts:
            plt.title(f"Corrente acumulada da rede — Cenario {cen} "
                      f"({'CHEIO 100%' if cen == 'A' else 'ESCALONADO'})")
            plt.xlabel("tempo decorrido (min)")
            plt.ylabel("soma acumulada da corrente da rede (mA)")
            plt.grid(alpha=0.3)
            plt.legend()
            plt.tight_layout()
            plt.savefig(OUTDIR / f"consumo_acumulado_rede_cenario_{cen}.png", dpi=110)
        plt.close()

    # ---------- figura: barras I por no + Q por no ----------
    valid = [(p, c, m) for p, c, m, _, _ in summary if m]
    labels = [f"{ABBR[p]}-{c}" for p, c, m in valid]
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))
    ipn = [m["i_per_node"] for _, _, m in valid]
    qpn = [m["q_per_node"] for _, _, m in valid]
    cols = [COLORS[p] for p, _, m in valid]
    for ax, vals, ttl, ylab in (
        (ax1, ipn, "Corrente media por no", "mA / no"),
        (ax2, qpn, "Carga gasta por no", "mAh / no"),
    ):
        bars = ax.bar(labels, vals, color=cols)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v, f"{v:.1f}",
                    ha="center", va="bottom", fontsize=9)
        ax.set_title(ttl)
        ax.set_ylabel(ylab)
        ax.grid(alpha=0.3, axis="y")
    fig.tight_layout()
    fig.savefig(OUTDIR / "consumo_por_no.png", dpi=110)
    plt.close(fig)

    print("\nFiguras salvas em:", OUTDIR)
    for f in sorted(OUTDIR.glob("consumo_*.png")):
        print("  ", f.name)


if __name__ == "__main__":
    main()
