#!/usr/bin/env python3
"""Analise definitiva dos 6 ensaios de 2026-06-20 a partir do InfluxDB.

Fonte preferida (vs sensores_iiot.db) porque tem as tags policy/run, que
identificam cada ensaio sem depender de janelas de tempo. Gera metricas +
figuras (curvas de bateria, comparacao do no mais fraco, FND).

Requer rede para 64.181.160.152:8086 (rode fora do sandbox).
"""
from influxdb_client import InfluxDBClient
from pathlib import Path
import collections, datetime as dt
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

ORG, BUCKET = "meu_tcc", "dados_esps"
URL = "http://64.181.160.152:8086"
TOKEN = Path(__file__).resolve().parents[1].joinpath("server/.influx_token").read_text().strip()
OUTDIR = Path(__file__).resolve().parent / "figures" / "ensaios_2026-06-20"
OUTDIR.mkdir(parents=True, exist_ok=True)

FND_THRESH = 5.0
SKIP_S = 60  # descarta transiente de reset/descoberta

# (rotulo, cenario, policy, run_id) — selecionados na descoberta.
ENSAIOS = [
    ("round_robin",     "A", "round_robin",     "31/12/1969 21:03:58"),
    ("round_robin",     "B", "round_robin",     "20/06/2026 10:59:21"),
    ("energy",          "A", "energy",          "31/12/1969 21:00:17"),
    ("energy",          "B", "energy",          "20/06/2026 12:04:05"),
    ("energy_cooldown", "A", "energy_cooldown", "31/12/1969 21:00:52"),
    ("energy_cooldown", "B", "energy_cooldown", "20/06/2026 13:00:03"),
]
COLORS = {"round_robin": "#1f77b4", "energy": "#d62728", "energy_cooldown": "#2ca02c"}

cli = InfluxDBClient(url=URL, token=TOKEN, org=ORG, timeout=60000)
qapi = cli.query_api()


def fetch(policy, run):
    """Retorna {node: [(elapsed_s, battery, papel)]} e t0, ordenado por tempo."""
    flux = f'''
from(bucket:"{BUCKET}")
 |> range(start: 2026-06-20T00:00:00Z, stop: 2026-06-21T00:00:00Z)
 |> filter(fn:(r)=>r._measurement=="consumo" and r._field=="battery_pct"
           and r.policy=="{policy}" and r.run=="{run}")
 |> keep(columns:["_time","_value","node","papel"])
'''
    rows = []
    for t in qapi.query(flux):
        for r in t.records:
            rows.append((r.get_time(), r.get_value(), r.values.get("node"),
                         r.values.get("papel")))
    if not rows:
        return {}, None
    rows.sort(key=lambda x: x[0])
    t0 = rows[0][0]
    by_node = collections.defaultdict(list)
    for tm, val, node, papel in rows:
        el = (tm - t0).total_seconds()
        by_node[node].append((el, val, papel))
    return by_node, t0


def fetch_current_by_role(policy, run):
    flux = f'''
from(bucket:"{BUCKET}")
 |> range(start: 2026-06-20T00:00:00Z, stop: 2026-06-21T00:00:00Z)
 |> filter(fn:(r)=>r._measurement=="consumo" and r._field=="current_ma"
           and r.policy=="{policy}" and r.run=="{run}")
 |> keep(columns:["_value","papel"])
'''
    agg = collections.defaultdict(lambda: [0, 0.0])
    for t in qapi.query(flux):
        for r in t.records:
            a = agg[r.values.get("papel")]
            a[0] += 1; a[1] += r.get_value()
    return {p: (n, s / n) for p, (n, s) in agg.items() if n}


def metrics(by_node):
    res = {}
    total_lead = 0
    for node, series in by_node.items():
        bats = [(el, b) for el, b, _ in series if el >= SKIP_S and b is not None]
        leads = sum(1 for el, _, p in series if el >= SKIP_S and p == "LEADER")
        total_lead += leads
        fnd = next((el / 60 for el, b in bats if b <= FND_THRESH), None)
        res[node] = {
            "b0": bats[0][1] if bats else None,
            "bN": bats[-1][1] if bats else None,
            "bmin": min((b for _, b in bats), default=None),
            "fnd": fnd, "lead": leads, "tmax": bats[-1][0] / 60 if bats else 0,
        }
    for node in res:
        res[node]["lead_share"] = 100 * res[node]["lead"] / total_lead if total_lead else 0
    return res


def main():
    summary = []
    all_data = {}
    for label, cen, policy, run in ENSAIOS:
        by_node, t0 = fetch(by_run := policy, run)
        m = metrics(by_node)
        cur = fetch_current_by_role(policy, run)
        all_data[(policy, cen)] = (by_node, m)
        fnds = [m[n]["fnd"] for n in m if m[n]["fnd"] is not None]
        rede_fnd = min(fnds) if fnds else None
        summary.append((label, cen, len(by_node), rede_fnd, m, cur))

        print("=" * 64)
        print(f"{policy} / {cen}  (run={run})  nos={len(by_node)}")
        for node in sorted(m):
            d = m[node]
            print(f"  {node[-4:]}  b0={d['b0']:.0f}  bN={d['bN']:.0f}  "
                  f"min={d['bmin']:.1f}  FND={'%.1f'%d['fnd'] if d['fnd'] else 'viv':>5}  "
                  f"lider={d['lead_share']:.0f}%")
        print(f"  FND rede: {('%.1f min'%rede_fnd) if rede_fnd else 'todos vivos'}"
              f"   | I LEADER={cur.get('LEADER',(0,0))[1]:.1f}mA "
              f"MEMBER={cur.get('MEMBER',(0,0))[1]:.1f}mA")

        # --- figura: bateria por no ---
        plt.figure(figsize=(9, 4.5))
        for node in sorted(by_node):
            pts = [(el / 60, b) for el, b, _ in by_node[node]
                   if el >= SKIP_S and b is not None]
            if pts:
                xs, ys = zip(*pts)
                plt.plot(xs, ys, label=node[-4:], lw=1.3)
        plt.axhline(FND_THRESH, color="gray", ls="--", lw=0.8, label=f"FND {FND_THRESH:.0f}%")
        plt.title(f"{policy} — Cenario {cen}  (bateria por no)")
        plt.xlabel("tempo decorrido (min)"); plt.ylabel("bateria (%)")
        plt.ylim(0, 100); plt.grid(alpha=0.3); plt.legend(fontsize=8, ncol=3)
        plt.tight_layout()
        f = OUTDIR / f"bateria_{policy}_{cen}.png"
        plt.savefig(f, dpi=110); plt.close()

    # --- comparacao: no mais fraco por cenario ---
    for cen in ("A", "B"):
        plt.figure(figsize=(9, 4.5))
        for policy in ("round_robin", "energy", "energy_cooldown"):
            if (policy, cen) not in all_data:
                continue
            by_node, _ = all_data[(policy, cen)]
            # no mais fraco por bucket de 30s
            buckets = collections.defaultdict(lambda: 999)
            for node, series in by_node.items():
                for el, b, _ in series:
                    if el >= SKIP_S and b is not None:
                        k = int(el // 30)
                        buckets[k] = min(buckets[k], b)
            pts = sorted((k * 30 / 60, v) for k, v in buckets.items())
            if pts:
                xs, ys = zip(*pts)
                plt.plot(xs, ys, label=policy, color=COLORS[policy], lw=1.6)
        plt.axhline(FND_THRESH, color="gray", ls="--", lw=0.8)
        plt.title(f"No mais fraco da rede — Cenario {cen} ({'CHEIO 100%' if cen=='A' else 'ESCALONADO'})")
        plt.xlabel("tempo decorrido (min)"); plt.ylabel("bateria do no mais fraco (%)")
        plt.ylim(0, 100); plt.grid(alpha=0.3); plt.legend()
        plt.tight_layout()
        plt.savefig(OUTDIR / f"comparacao_no_fraco_cenario_{cen}.png", dpi=110); plt.close()

    # --- FND bar chart ---
    plt.figure(figsize=(8, 4.5))
    ABBR = {"round_robin": "RR", "energy": "EN", "energy_cooldown": "EC"}
    labels = [f"{ABBR[s[0]]}-{s[1]}" for s in summary]
    fnds = [s[3] if s[3] else 0 for s in summary]
    bars = plt.bar(labels, fnds, color=[COLORS[s[0]] for s in summary])
    for b, v, s in zip(bars, fnds, summary):
        txt = f"{v:.1f}" if s[3] else "viv"
        plt.text(b.get_x() + b.get_width() / 2, v + 0.3, txt, ha="center", fontsize=9)
    plt.ylabel("FND (min)"); plt.title(f"FND por ensaio (1o no <= {FND_THRESH:.0f}%)")
    plt.grid(alpha=0.3, axis="y"); plt.tight_layout()
    plt.savefig(OUTDIR / "fnd_por_ensaio.png", dpi=110); plt.close()

    print("\nFiguras salvas em:", OUTDIR)
    for f in sorted(OUTDIR.glob("*.png")):
        print("  ", f.name)


if __name__ == "__main__":
    main()
