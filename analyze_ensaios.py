#!/usr/bin/env python3
"""Analise dos 6 ensaios a partir do SQLite (sensores_iiot.db).

As janelas sao DERIVADAS DOS DADOS: cada onda de reset (bateria salta para
~100 ou nivel escalonado) marca o inicio de um ensaio. Isso e' mais confiavel
que os horarios anotados, que nao batem com os resets reais.

`timestamp` no banco e' UTC. Nao ha colunas policy/run no SQLite.
"""
import sqlite3
from datetime import datetime

DB = "sensores_iiot.db"
FND_THRESH = 5.0

NODES = ["1cdbd4c43fdc", "1cdbd4c442a4", "1cdbd4c589ec",
         "a0f262a41d70", "a0f262a49f48"]
SHORT = {m: m[-4:] for m in NODES}

# Fronteiras (UTC) derivadas das ondas de reset detectadas nos dados.
# (rotulo, inicio, fim)
ENSAIOS = [
    ("round_robin / A (CHEIO)",      "2026-06-20 13:28:14", "2026-06-20 13:59:40"),
    ("round_robin / B (ESCAL.)",     "2026-06-20 13:59:40", "2026-06-20 14:35:35"),
    ("energy / A (CHEIO)",           "2026-06-20 14:35:35", "2026-06-20 15:04:24"),
    ("energy / B (ESCAL.)",          "2026-06-20 15:04:24", "2026-06-20 15:32:10"),
    ("energy_cooldown / A (CHEIO)",  "2026-06-20 15:32:10", "2026-06-20 16:00:22"),
    ("energy_cooldown / B (ESCAL.)", "2026-06-20 16:00:22", "2026-06-20 16:20:00"),
]
SKIP_S = 60  # ignora os primeiros 60s (transiente de reset/descoberta)


def parse(ts):
    return datetime.strptime(ts, "%Y-%m-%d %H:%M:%S")


def fetch(con, start, end):
    cur = con.execute(
        """SELECT topico, corrente_ma, bateria_pct, timestamp, papel
           FROM leituras WHERE timestamp>=? AND timestamp<? AND topico LIKE '/tcc/main/%'
           ORDER BY timestamp""", (start, end))
    rows = []
    for topico, corr, bat, ts, papel in cur:
        mac = topico.rsplit("/", 1)[-1]
        if mac in NODES:
            rows.append((mac, corr, bat, ts, papel or ""))
    return rows


def analyze(label, rows, start):
    print("=" * 72)
    print(f"ENSAIO: {label}   [{start} UTC, dur {ENSAIOS_DUR.get(label,'?')}]")
    print("-" * 72)
    if not rows:
        print("  (sem dados)"); return None

    t0 = parse(start)
    pn = {m: {"n": 0, "b0": None, "bN": None, "bmin": 999, "fnd": None,
              "lead": 0, "reset_here": False, "isum": 0.0, "icnt": 0} for m in NODES}
    leaders_sec = {}

    prev_bat = {}
    for mac, corr, bat, ts, papel in rows:
        tt = parse(ts)
        elapsed = (tt - t0).total_seconds()
        d = pn[mac]
        # marca participante se a bateria saltou pra cima no inicio (reset desta janela)
        if bat is not None:
            if mac in prev_bat and bat - prev_bat[mac] > 8 and elapsed < 180:
                d["reset_here"] = True
            prev_bat[mac] = bat
        if elapsed < SKIP_S:
            continue
        d["n"] += 1
        if corr is not None:
            d["isum"] += corr; d["icnt"] += 1
        if bat is not None:
            if d["b0"] is None:
                d["b0"] = bat
            d["bN"] = bat
            d["bmin"] = min(d["bmin"], bat)
            if bat <= FND_THRESH and d["fnd"] is None:
                d["fnd"] = elapsed / 60.0
        if papel == "LEADER":
            d["lead"] += 1
            leaders_sec.setdefault(ts, set()).add(mac)

    # participante = teve reset no inicio OU comecou com bateria alta (>60)
    part = [m for m in NODES if pn[m]["n"] > 0 and
            (pn[m]["reset_here"] or (pn[m]["b0"] or 0) > 60)]
    carry = [m for m in NODES if pn[m]["n"] > 0 and m not in part]

    total_lead = sum(pn[m]["lead"] for m in part) or 1
    print(f"  Participantes: {len(part)} {[SHORT[m] for m in part]}"
          + (f"   | carryover/ignorados: {[SHORT[m] for m in carry]}" if carry else ""))
    print(f"  {'no':>6} {'b ini':>6} {'b fim':>6} {'b min':>6} {'FND':>6} {'lider%':>7} {'I med':>7}")
    for m in part:
        d = pn[m]
        fnd = f"{d['fnd']:.1f}" if d["fnd"] is not None else "viv"
        imed = f"{d['isum']/d['icnt']:.0f}" if d["icnt"] else "-"
        sh = 100.0 * d["lead"] / total_lead
        print(f"  {SHORT[m]:>6} {d['b0']:>6.1f} {d['bN']:>6.1f} {d['bmin']:>6.1f} "
              f"{fnd:>6} {sh:>6.1f}% {imed:>6}")

    fnds = [pn[m]["fnd"] for m in part if pn[m]["fnd"] is not None]
    rede_fnd = min(fnds) if fnds else None
    multi = {ts: s for ts, s in leaders_sec.items() if len(s) > 1}
    print("-" * 72)
    print(f"  >> FND da rede: " + (f"{rede_fnd:.1f} min" if rede_fnd is not None
          else "nao atingido (todos vivos)"))
    print(f"  >> Mortes na janela: {len(fnds)}/{len(part)} nos cruzaram {FND_THRESH}%")
    print(f"  >> Split-brain (>=2 lideres/seg): {len(multi)}")
    for ts, s in list(multi.items())[:3]:
        print(f"        {ts}: {[SHORT[m] for m in s]}")
    if len(part) > 1:
        shares = [100.0 * pn[m]["lead"] / total_lead for m in part]
        print(f"  >> Lideranca: ideal {100/len(part):.1f}%/no | amplitude {max(shares)-min(shares):.1f} pp")
    return rede_fnd


ENSAIOS_DUR = {}
for lbl, s, e in ENSAIOS:
    ENSAIOS_DUR[lbl] = f"{(parse(e)-parse(s)).total_seconds()/60:.0f} min"


def main():
    con = sqlite3.connect(DB)
    resumo = []
    for label, s, e in ENSAIOS:
        resumo.append((label, analyze(label, fetch(con, s, e), s)))
    print("=" * 72)
    print(f"RESUMO FND (min ate' 1o no <= {FND_THRESH}%)  — comparar A com A, B com B")
    print("-" * 72)
    for label, fnd in resumo:
        print(f"  {label:<32} {('%.1f min' % fnd) if fnd is not None else 'todos vivos'}")
    con.close()


if __name__ == "__main__":
    main()
