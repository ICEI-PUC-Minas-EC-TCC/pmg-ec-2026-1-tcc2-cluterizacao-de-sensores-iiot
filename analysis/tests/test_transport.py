import random
from analysis.simulator.transport import Transport, Message
from analysis.simulator.policies import ZERO_MAC

A = bytes([0,0,0,0,0,1]); B = bytes([0,0,0,0,0,2]); C = bytes([0,0,0,0,0,3])

def test_unicast_reliable_by_default():
    t = Transport(random.Random(0), [A,B,C])
    t.unicast(B, Message(kind="reading", src=A))
    assert len(t.deliver_inbox(B)) == 1
    assert t.deliver_inbox(B) == []  # esvaziou

def test_broadcast_reaches_others_not_self():
    t = Transport(random.Random(0), [A,B,C], ping_loss=0.0)
    t.broadcast(A, Message(kind="ping", src=A))
    assert len(t.deliver_inbox(B)) == 1
    assert len(t.deliver_inbox(C)) == 1
    assert t.deliver_inbox(A) == []

def test_broadcast_loss_drops_some():
    t = Transport(random.Random(1), [A,B], ping_loss=1.0)  # perde tudo
    t.broadcast(A, Message(kind="ping", src=A))
    assert t.deliver_inbox(B) == []
