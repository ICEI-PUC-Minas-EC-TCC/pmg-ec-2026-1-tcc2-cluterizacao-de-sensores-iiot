"""Compara o leader_policy.cpp REAL (compilado) com o port Python policies.py."""
import os, shutil, subprocess, random
import pytest
from analysis.simulator import policies as P

HERE = os.path.dirname(__file__)
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ORACLE = os.path.join(HERE, "oracle")
STUBS = os.path.join(ORACLE, "stubs")
BUILD = os.path.join(ORACLE, "build")
SRC = [os.path.join(ROOT, "main", "src", "Application", "leader_policy.cpp"),
       os.path.join(ORACLE, "energy_double.cpp"),
       os.path.join(ORACLE, "oracle_main.cpp")]
DEFS = {"round_robin": [], "energy": ["-DCONFIG_LEADER_POLICY_ENERGY=1"],
        "energy_cooldown": ["-DCONFIG_LEADER_POLICY_ENERGY_COOLDOWN=1"]}

@pytest.fixture(scope="module")
def oracles():
    cxx = shutil.which("g++") or shutil.which("clang++")
    if cxx is None:
        pytest.skip("compilador C++ (g++/clang++) ausente — conformância pulada")
    if not os.path.exists(SRC[0]):
        pytest.skip("firmware de liderança ausente (main/src/Application/leader_policy.cpp) "
                    "— conformância pulada (depende do firmware estar presente)")
    os.makedirs(BUILD, exist_ok=True)
    found = {}
    for pol, defs in DEFS.items():
        out = os.path.join(BUILD, f"oracle_{pol}" + (".exe" if os.name == "nt" else ""))
        cmd = [cxx, "-std=c++17", "-I", os.path.join(ROOT, "main", "include"),
               "-I", STUBS, *defs, *SRC, "-o", out]
        subprocess.run(cmd, check=True, capture_output=True, text=True)
        found[pol] = out
    return found

def _mac(i): return bytes([0,0,0,0,0,i])
def _hex(m): return m.hex()

def _scenarios(rng, n=200):
    out = []
    for _ in range(n):
        size = rng.randint(2, 5)
        macs = [_mac(i) for i in rng.sample(range(1, 9), size)]
        residuals = {m: rng.randint(0, 100000) for m in macs}
        cools = {m: rng.random() < 0.3 for m in macs}
        valids = {m: rng.random() < 0.8 for m in macs}
        current = rng.choice(macs)
        own = rng.choice(macs)
        valids[own] = True  # firmware: get_residual() do próprio nó é sempre válido
        out.append((own, current, macs, residuals, cools, valids))
    # cenários engendrados: own==current (excluído) + peers inválidos => !found => fallback RR
    for _ in range(20):
        size = rng.randint(2, 5)
        macs = [_mac(i) for i in rng.sample(range(1, 9), size)]
        residuals = {m: rng.randint(0, 100000) for m in macs}
        cools = {m: False for m in macs}
        valids = {m: False for m in macs}
        current = rng.choice(macs)
        own = current
        valids[own] = True
        out.append((own, current, macs, residuals, cools, valids))
    return out

def _py_pick(pol, own, current, macs, residuals, cools, valids):
    cluster = sorted(macs)
    cd = P.CooldownTracker()
    for m in macs:
        if cools[m]:
            cd.on_became_leader(m, now_ms=0)
    return P.pick_next_leader(
        pol, cluster, current, own,
        residual_of=lambda m: (residuals[m], valids[m]),
        cooldown=cd, now_ms=1000)

def _cpp_pick(exe, own, current, macs, residuals, cools, valids):
    peers = ",".join(
        f"{_hex(m)}:{residuals[m]}:{1 if cools[m] else 0}:{1 if valids[m] else 0}"
        for m in macs)
    line = f"{_hex(own)};{_hex(current)};{peers}\n"
    res = subprocess.run([exe], input=line, capture_output=True, text=True, check=True)
    return bytes.fromhex(res.stdout.strip())

@pytest.mark.parametrize("pol", ["round_robin", "energy", "energy_cooldown"])
def test_policy_matches_firmware(oracles, pol):
    rng = random.Random(12345)
    for own, current, macs, residuals, cools, valids in _scenarios(rng):
        cpp = _cpp_pick(oracles[pol], own, current, macs, residuals, cools, valids)
        py = _py_pick(pol, own, current, macs, residuals, cools, valids)
        assert cpp == py, f"divergência {pol}: cpp={cpp.hex()} py={py.hex()}"
