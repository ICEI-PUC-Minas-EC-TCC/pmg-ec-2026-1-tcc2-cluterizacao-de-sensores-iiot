# analysis/tests/test_nodes.py
import random
from analysis.simulator.transport import Transport
from analysis.simulator.energy import ABSTRACT
from analysis.simulator.nodes import Node
from analysis import params

A = bytes([0,0,0,0,0,1]); B = bytes([0,0,0,0,0,2])

def make_pair(strategy="round_robin"):
    rng = random.Random(0)
    t = Transport(rng, [A,B], ping_loss=0.0, unicast_loss=0.0)
    events = []
    emit = lambda node, now, ev: events.append((node.mac, ev, now))
    a = Node(A, ABSTRACT, strategy, t, emit)
    b = Node(B, ABSTRACT, strategy, t, emit)
    return t, [a, b], events

def run(nodes, steps):
    for k in range(steps):
        for n in nodes:
            n.step(now_ms=k * params.LOOP_PERIOD_MS)

def test_smallest_mac_becomes_leader():
    t, nodes, events = make_pair()
    run(nodes, steps=40)  # > DISCOVERY_WINDOW + alguns pings
    a, b = nodes
    assert a.role == "leader" and b.role == "member"
    assert any(ev == "became_leader" and mac == A for mac, ev, _ in events)

def test_leader_rotates_after_term():
    t, nodes, events = make_pair()
    run(nodes, steps=200)  # cobre > 1 mandato (10 s = 100 passos)
    leaders = [mac for mac, ev, _ in events if ev == "became_leader"]
    assert A in leaders and B in leaders  # liderança rotacionou

def test_leader_energy_drains_faster_than_member():
    t, nodes, events = make_pair()
    run(nodes, steps=80)  # ainda no 1º mandato de A
    a, b = nodes
    assert a.energy.residual < b.energy.residual
