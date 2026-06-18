"""Fidelidade: analysis/params.py espelha as constantes do firmware.

O firmware é a fonte de verdade. Este teste extrai cada valor direto dos
.cpp / Kconfig.projbuild e compara com o que está em params.py, travando a
regressão de dessincronização (a conformância não pega isto: ela só exercita
now_ms=1 s, dentro do cooldown para qualquer valor >= 1 s).

Cada constante vira um caso parametrizado; arquivos ausentes pulam (skip)
individualmente, espelhando o comportamento da conformância.
"""
import os
import re

import pytest

from analysis import params

HERE = os.path.dirname(__file__)
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))

# Tipos de extração suportados.
CONSTEXPR = "constexpr"      # static constexpr T NOME = <valor>;
VTASK = "vtask"              # vTaskDelay(<valor> / portTICK_PERIOD_MS)
HASELAPSED = "haselapsed"    # timer.hasElapsed(<valor>)
KDEFAULT = "kdefault"        # config NOME ... default <valor>  (Kconfig)

# (nome em params.py, tipo, caminho relativo, símbolo no firmware)
SPECS = [
    # role_service.cpp — constexpr nomeadas
    ("DISCOVERY_WINDOW_MS", CONSTEXPR, "main/src/Application/role_service.cpp", "DISCOVERY_WINDOW_MS"),
    ("TERM_DURATION_MS", CONSTEXPR, "main/src/Application/role_service.cpp", "TERM_DURATION_MS"),
    ("ISOLATION_TIMEOUT_MS", CONSTEXPR, "main/src/Application/role_service.cpp", "ISOLATION_TIMEOUT_MS"),
    ("ROTATE_RETRIES", CONSTEXPR, "main/src/Application/role_service.cpp", "ROTATE_RETRIES"),
    ("ROTATE_RETRY_MS", CONSTEXPR, "main/src/Application/role_service.cpp", "ROTATE_RETRY_MS"),
    # leader_policy.cpp
    ("COOLDOWN_MS", CONSTEXPR, "main/src/Application/leader_policy.cpp", "COOLDOWN_MS"),
    # energy_service.cpp (nota: ENERGY_TICK_PERIOD_MS <- TICK_PERIOD_MS no firmware)
    ("INITIAL_BUDGET", CONSTEXPR, "main/src/Application/energy_service.cpp", "INITIAL_BUDGET"),
    ("COST_MQTT", CONSTEXPR, "main/src/Application/energy_service.cpp", "COST_MQTT"),
    ("COST_ESPNOW_SEND", CONSTEXPR, "main/src/Application/energy_service.cpp", "COST_ESPNOW_SEND"),
    ("COST_TICK", CONSTEXPR, "main/src/Application/energy_service.cpp", "COST_TICK"),
    ("ENERGY_TICK_PERIOD_MS", CONSTEXPR, "main/src/Application/energy_service.cpp", "TICK_PERIOD_MS"),
    ("PEER_TTL_MS", CONSTEXPR, "main/src/Application/energy_service.cpp", "PEER_TTL_MS"),
    # reading_service.cpp
    ("READING_INTERVAL_MS", CONSTEXPR, "main/src/Application/reading_service.cpp", "READING_INTERVAL_MS"),
    # derivadas (não são constexpr nomeadas)
    ("LOOP_PERIOD_MS", VTASK, "main/src/Application/application_controller.cpp", None),
    ("PING_PERIOD_MS", HASELAPSED, "main/src/Application/discover_service.cpp", None),
    ("BATTERY_CAPACITY_MAH", KDEFAULT, "main/Kconfig.projbuild", "AMMETER_BATTERY_CAPACITY_MAH"),
    ("BATTERY_VOLTAGE_MV", KDEFAULT, "main/Kconfig.projbuild", "AMMETER_BATTERY_VOLTAGE_MV"),
]

# Parâmetros só de simulação: sem contraparte numérica no firmware (derivam de
# comentários qualitativos). Cobertos pelo teste de cobertura, não pela extração.
SIM_ONLY = {"PING_BROADCAST_LOSS", "UNICAST_LOSS"}


def _read(relpath):
    with open(os.path.join(ROOT, *relpath.split("/")), encoding="utf-8", errors="replace") as f:
        return f.read()


def _norm(digits):
    """Normaliza literais inteiros C++/Python: remove separadores ' e _."""
    return int(digits.replace("'", "").replace("_", ""))


def _extract(kind, relpath, symbol):
    src = _read(relpath)
    if kind == CONSTEXPR:
        m = re.search(r"constexpr\s+\w+\s+" + re.escape(symbol) + r"\s*=\s*([0-9][0-9_']*)", src)
    elif kind == VTASK:
        m = re.search(r"vTaskDelay\(\s*([0-9][0-9_']*)\s*/\s*portTICK_PERIOD_MS", src)
    elif kind == HASELAPSED:
        m = re.search(r"hasElapsed\(\s*([0-9][0-9_']*)\s*\)", src)
    elif kind == KDEFAULT:
        m = re.search(r"config\s+" + re.escape(symbol) + r"\b.*?default\s+([0-9][0-9_']*)", src, re.DOTALL)
    else:
        raise ValueError(f"tipo de extração desconhecido: {kind}")
    assert m, f"não encontrei {symbol or kind} em {relpath}"
    return _norm(m.group(1))


@pytest.mark.parametrize("name,kind,relpath,symbol", SPECS, ids=[s[0] for s in SPECS])
def test_param_espelha_firmware(name, kind, relpath, symbol):
    if not os.path.exists(os.path.join(ROOT, *relpath.split("/"))):
        pytest.skip(f"firmware ausente: {relpath}")
    expected = _extract(kind, relpath, symbol)
    actual = getattr(params, name)
    assert actual == expected, (
        f"{name}: params.py={actual} diverge do firmware={expected} ({relpath})")


def test_cobertura_completa_de_params():
    """Toda constante pública de params.py deve estar coberta pela extração de
    fidelidade ou marcada explicitamente como parâmetro de simulação — assim
    uma constante nova não escapa silenciosamente do contrato."""
    publicas = {n for n in dir(params) if n[:1].isalpha() and n.isupper()}
    cobertas = {name for name, *_ in SPECS} | SIM_ONLY
    faltando = publicas - cobertas
    assert not faltando, f"constantes de params.py sem cobertura de fidelidade: {sorted(faltando)}"
