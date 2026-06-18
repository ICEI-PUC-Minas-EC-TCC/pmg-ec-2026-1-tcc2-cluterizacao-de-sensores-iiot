"""Correntes calibradas por papel (medições do ammeter) — ponto único de verdade.

Origem: PLACEHOLDER (estimativa) até a medição no laboratório da PUC. Para
atualizar, rode `python -m analysis.calibration <dir_de_logs>` e cole os três
valores abaixo, registrando a data e os arquivos de origem.
"""

import argparse
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path
from statistics import mean, pstdev

# Correntes por papel, em mA. PLACEHOLDER até a calibração real (item 2).
LEADER_MA = 120.0
MEMBER_MA = 25.0
IDLE_MA = 8.0

_CALIB_RE = re.compile(r"CALIB:\s*role=(\w+)\s+I=(-?\d+(?:\.\d+)?)mA")
_ROLE_RE = re.compile(r"ROLE_SERVICE:\s*Role:\s*(LEADER|MEMBER)")
_AMMETER_RE = re.compile(r"AMMETER\w*:\s*I=(-?\d+(?:\.\d+)?)mA")
_TS_RE = re.compile(r"^\s*[IWED]\s*\((\d+)\)")


@dataclass(frozen=True)
class RoleStats:
    role: str
    mean_ma: float
    std_ma: float
    count: int


def _ts(line):
    m = _TS_RE.search(line)
    return int(m.group(1)) if m else None


def _settled(ts, transition_ts, settle_ms):
    if settle_ms <= 0 or ts is None or transition_ts is None:
        return True
    return ts - transition_ts >= settle_ms


def _iter_calib(lines, settle_ms=0):
    """Linhas CALIB: cada uma traz o papel. Marca transição quando o papel muda
    entre amostras consecutivas e descarta o transiente."""
    role = None
    transition_ts = None
    for line in lines:
        m = _CALIB_RE.search(line)
        if not m:
            continue
        ts = _ts(line)
        new_role, cur = m.group(1).upper(), float(m.group(2))
        if new_role != role:
            role, transition_ts = new_role, ts
        if _settled(ts, transition_ts, settle_ms):
            yield role, cur


def _iter_fallback(lines, settle_ms=0):
    """Sem CALIB: papel das transições ROLE_SERVICE; corrente das linhas AMMETER.
    Antes da 1a eleição (ou após reboot) o papel é IDLE."""
    role = "IDLE"
    transition_ts = None
    last_ts = None
    for line in lines:
        ts = _ts(line)
        if ts is not None and last_ts is not None and ts < last_ts:
            role, transition_ts = "IDLE", ts  # reboot
        if ts is not None:
            last_ts = ts
        mr = _ROLE_RE.search(line)
        if mr:
            new_role = mr.group(1).upper()
            if new_role != role:
                role, transition_ts = new_role, ts
            continue
        ma = _AMMETER_RE.search(line)
        if ma and _settled(ts, transition_ts, settle_ms):
            yield role, float(ma.group(1))


def parse_logs(paths, settle_ms=0):
    """Lê os logs e agrega a corrente por papel. Prefere CALIB; cai para
    ROLE_SERVICE+AMMETER sem CALIB. `settle_ms` descarta o transiente logo após
    cada troca de papel."""
    buckets = {}
    for p in paths:
        lines = Path(p).read_text(errors="replace").splitlines()
        has_calib = any(_CALIB_RE.search(l) for l in lines)
        it = (_iter_calib(lines, settle_ms) if has_calib
              else _iter_fallback(lines, settle_ms))
        for role, cur in it:
            buckets.setdefault(role, []).append(cur)
    return {
        r: RoleStats(r, mean(v), pstdev(v) if len(v) > 1 else 0.0, len(v))
        for r, v in sorted(buckets.items())
    }


def parse_db(db_path, settle_samples=5, min_ma=0.5):
    """Le as leituras gravadas pelo server_mqtt.py (SQLite) e agrega a corrente
    por papel. Cada no publica seu papel (LEADER/MEMBER) no payload MQTT; o
    IDLE nao chega por MQTT (Wi-Fi desligado quando isolado). Por no, descarta
    as primeiras `settle_samples` amostras apos cada troca de papel (transiente
    de Wi-Fi) e ignora leituras <= `min_ma` (INA219 falho publica 0.0)."""
    con = sqlite3.connect(db_path)
    try:
        rows = con.execute(
            "SELECT topico, papel, corrente_ma FROM leituras "
            "WHERE papel IS NOT NULL AND corrente_ma IS NOT NULL ORDER BY id"
        ).fetchall()
    finally:
        con.close()
    buckets = {}
    state = {}  # topico -> (papel_atual, n_amostras_no_papel)
    for topico, papel, cur in rows:
        papel = papel.upper()
        role, since = state.get(topico, (None, 0))
        since = 0 if papel != role else since + 1
        state[topico] = (papel, since)
        if cur <= min_ma:  # INA219 falho / sem medicao
            continue
        if since < settle_samples:  # transiente apos troca de papel
            continue
        buckets.setdefault(papel, []).append(cur)
    return {
        r: RoleStats(r, mean(v), pstdev(v) if len(v) > 1 else 0.0, len(v))
        for r, v in sorted(buckets.items())
    }


_ROLES = ("LEADER", "MEMBER", "IDLE")


def _format_report(stats):
    out = []
    for role in _ROLES:
        s = stats.get(role)
        if s:
            out.append(f"# {role:<6} n={s.count:<4} "
                       f"media={s.mean_ma:.2f}mA  sigma={s.std_ma:.2f}mA")
        else:
            out.append(f"# {role:<6} (sem amostras - faltou medir este papel)")
    out.append("")
    out.append("# Cole em analysis/calibration.py:")
    for role, const in (("LEADER", "LEADER_MA"), ("MEMBER", "MEMBER_MA"),
                        ("IDLE", "IDLE_MA")):
        s = stats.get(role)
        val = f"{s.mean_ma:.1f}" if s else "None  # FALTOU medir"
        out.append(f"{const} = {val}")
    return "\n".join(out)


def main(argv=None):
    ap = argparse.ArgumentParser(
        prog="python -m analysis.calibration",
        description="Extrai as correntes por papel dos logs seriais.")
    ap.add_argument("paths", nargs="+",
                    help="diretorio/arquivos .log, ou o .db do server_mqtt.py")
    ap.add_argument("--settle-ms", type=int, default=5000,
                    help="logs: descarta amostras nos primeiros ms apos troca de papel")
    ap.add_argument("--settle-samples", type=int, default=5,
                    help="db: descarta as N primeiras amostras por no apos troca de papel")
    a = ap.parse_args(argv)

    # Fonte SQLite (server_mqtt.py): traz LEADER e MEMBER; IDLE nao chega por MQTT.
    db_paths = [p for p in a.paths if str(p).endswith(".db")]
    if db_paths:
        if len(a.paths) > 1:
            print("Passe um unico .db (ou apenas arquivos de log, nao misture).")
            return 1
        db = db_paths[0]
        stats = parse_db(db, settle_samples=a.settle_samples)
        print(f"# Fonte: {db}  (DB MQTT, settle={a.settle_samples} amostras/no)")
        print(_format_report(stats))
        return 0

    files = []
    for p in a.paths:
        pp = Path(p)
        files.extend(sorted(pp.glob("*.log")) if pp.is_dir() else [pp])
    if not files:
        print("Nenhum log encontrado. Uso: python -m analysis.calibration "
              "<dir|arquivos.log|arquivo.db> [--settle-ms N | --settle-samples N]")
        return 1

    stats = parse_logs(files, settle_ms=a.settle_ms)
    print(f"# Fonte: {', '.join(str(f) for f in files)}  (settle={a.settle_ms}ms)")
    print(_format_report(stats))
    return 0


if __name__ == "__main__":
    sys.exit(main())
