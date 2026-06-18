"""Correntes calibradas por papel (medições do ammeter) — ponto único de verdade.

Origem: PLACEHOLDER (estimativa) até a medição no laboratório da PUC. Para
atualizar, rode `python -m analysis.calibration <dir_de_logs>` e cole os três
valores abaixo, registrando a data e os arquivos de origem.
"""

import argparse
import re
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
    ap.add_argument("paths", nargs="+", help="diretorio de logs ou arquivos .log")
    ap.add_argument("--settle-ms", type=int, default=5000,
                    help="descarta amostras nos primeiros ms apos troca de papel")
    a = ap.parse_args(argv)

    files = []
    for p in a.paths:
        pp = Path(p)
        files.extend(sorted(pp.glob("*.log")) if pp.is_dir() else [pp])
    if not files:
        print("Nenhum log encontrado. "
              "Uso: python -m analysis.calibration <dir|arquivos> [--settle-ms N]")
        return 1

    stats = parse_logs(files, settle_ms=a.settle_ms)
    print(f"# Fonte: {', '.join(str(f) for f in files)}  (settle={a.settle_ms}ms)")
    print(_format_report(stats))
    return 0


if __name__ == "__main__":
    sys.exit(main())
