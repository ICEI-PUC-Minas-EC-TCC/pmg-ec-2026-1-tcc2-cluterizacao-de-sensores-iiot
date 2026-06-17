from analysis.simulator import policies as p
from analysis import params

A = bytes([0,0,0,0,0,1]); B = bytes([0,0,0,0,0,2]); C = bytes([0,0,0,0,0,3])

def test_round_robin_next_in_ring():
    assert p.pick_round_robin([A,B,C], A) == B
    assert p.pick_round_robin([A,B,C], C) == A  # dá a volta

def test_energy_picks_highest_excluding_current():
    res = {A:(10,True), B:(99,True), C:(50,True)}
    out = p.pick_energy([A,B,C], current_leader=A, own_mac=A,
                        residual_of=lambda m: res[m],
                        in_cooldown=lambda m: False, exclude_in_cooldown=False)
    assert out == B

def test_energy_falls_back_to_round_robin_without_valid_samples():
    out = p.pick_energy([A,B,C], current_leader=A, own_mac=A,
                        residual_of=lambda m: (0, False),
                        in_cooldown=lambda m: False, exclude_in_cooldown=False)
    assert out == p.pick_round_robin([A,B,C], A)

def test_cooldown_excludes_recent_leader():
    cd = p.CooldownTracker()
    cd.on_became_leader(B, now_ms=0)
    assert cd.in_cooldown(B, now_ms=params.COOLDOWN_MS - 1) is True
    assert cd.in_cooldown(B, now_ms=params.COOLDOWN_MS + 1) is False
    res = {A:(10,True), B:(99,True), C:(50,True)}
    out = p.pick_energy([A,B,C], current_leader=A, own_mac=A,
                        residual_of=lambda m: res[m],
                        in_cooldown=lambda m: cd.in_cooldown(m, 100),
                        exclude_in_cooldown=True)
    assert out == C  # B está em cooldown, então o 2º maior vence
