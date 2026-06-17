"""ESP-NOW falso: broadcast (PING) lossy, unicast (READING/ROTATE) confiável.

As mensagens enviadas num passo entram na fila do destino e são entregues no
próximo passo (espelha a recepção assíncrona via rx_callback entre iterações
do loop do firmware)."""
from dataclasses import dataclass
from analysis import params
from analysis.simulator.policies import ZERO_MAC

@dataclass
class Message:
    kind: str
    src: bytes
    residual: float = 0.0
    announced_leader: bytes = ZERO_MAC
    temperature: float = 0.0
    current_ma: float = 0.0
    battery_pct: float = 0.0
    next_leader: bytes = ZERO_MAC

class Transport:
    def __init__(self, rng, nodes_macs, ping_loss=params.PING_BROADCAST_LOSS,
                 unicast_loss=params.UNICAST_LOSS):
        self._rng = rng
        self._macs = list(nodes_macs)
        self._ping_loss = ping_loss
        self._unicast_loss = unicast_loss
        self._inbox: dict[bytes, list] = {m: [] for m in self._macs}

    def broadcast(self, src: bytes, msg: Message) -> None:
        for mac in self._macs:
            if mac == src:
                continue
            if self._rng.random() >= self._ping_loss:
                self._inbox[mac].append(msg)

    def unicast(self, dst: bytes, msg: Message) -> None:
        if dst in self._inbox and self._rng.random() >= self._unicast_loss:
            self._inbox[dst].append(msg)

    def deliver_inbox(self, mac: bytes) -> list:
        msgs = self._inbox[mac]
        self._inbox[mac] = []
        return msgs
