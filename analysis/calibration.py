"""Correntes calibradas por papel (medições do ammeter) — ponto único de verdade.

Origem: PLACEHOLDER (estimativa) até a medição no laboratório da PUC. Para
atualizar, rode `python -m analysis.calibration <dir_de_logs>` e cole os três
valores abaixo, registrando a data e os arquivos de origem.
"""

import re
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


def _iter_calib(lines):
    """Linhas CALIB: cada uma já traz o papel; nada a inferir."""
    for line in lines:
        m = _CALIB_RE.search(line)
        if m:
            yield m.group(1).upper(), float(m.group(2))


def _iter_fallback(lines):
    """Sem CALIB: papel inferido das transições ROLE_SERVICE; corrente das
    linhas AMMETER. Antes da 1a eleição (ou após reboot) o papel é IDLE."""
    role = "IDLE"
    last_ts = None
    for line in lines:
        ts = _ts(line)
        if ts is not None and last_ts is not None and ts < last_ts:
            role = "IDLE"  # reboot: timestamp volta a subir do zero
        if ts is not None:
            last_ts = ts
        mr = _ROLE_RE.search(line)
        if mr:
            role = mr.group(1).upper()
            continue
        ma = _AMMETER_RE.search(line)
        if ma:
            yield role, float(ma.group(1))


def parse_logs(paths):
    """Lê os logs e agrega a corrente por papel. Prefere as linhas CALIB; cai
    para ROLE_SERVICE+AMMETER quando o arquivo não tem CALIB."""
    buckets = {}
    for p in paths:
        lines = Path(p).read_text(errors="replace").splitlines()
        has_calib = any(_CALIB_RE.search(l) for l in lines)
        it = _iter_calib(lines) if has_calib else _iter_fallback(lines)
        for role, cur in it:
            buckets.setdefault(role, []).append(cur)
    return {
        r: RoleStats(r, mean(v), pstdev(v) if len(v) > 1 else 0.0, len(v))
        for r, v in sorted(buckets.items())
    }
