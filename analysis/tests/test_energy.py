from analysis.simulator import energy
from analysis import params


def test_abstract_costs_match_firmware():
    e = energy.EnergyService(energy.ABSTRACT)
    assert e.residual == params.INITIAL_BUDGET
    e.on_mqtt_publish(); assert e.residual == params.INITIAL_BUDGET - params.COST_MQTT
    e.on_espnow_send(); assert e.residual == params.INITIAL_BUDGET - params.COST_MQTT - params.COST_ESPNOW_SEND


def test_tick_gated_by_period():
    e = energy.EnergyService(energy.ABSTRACT)
    e.tick(now_ms=500); assert e.residual == params.INITIAL_BUDGET           # < período
    e.tick(now_ms=1000); assert e.residual == params.INITIAL_BUDGET - 1      # cruzou 1s


def test_residual_floors_at_zero():
    e = energy.EnergyService(energy.calibrated(1, 1, 1))
    e.residual = 3
    e.on_mqtt_publish()  # custo calibrado pode exceder; nunca negativa
    assert e.residual >= 0


def test_peer_ttl_expires():
    e = energy.EnergyService(energy.ABSTRACT)
    e.on_peer_energy(b"\x00\x00\x00\x00\x00\x02", 42, now_ms=0)
    assert e.get_peer_residual(b"\x00\x00\x00\x00\x00\x02", now_ms=params.PEER_TTL_MS - 1) == (42, True)
    assert e.get_peer_residual(b"\x00\x00\x00\x00\x00\x02", now_ms=params.PEER_TTL_MS + 1) == (0.0, False)
