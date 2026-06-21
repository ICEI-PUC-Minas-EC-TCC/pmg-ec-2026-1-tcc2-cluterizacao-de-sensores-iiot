"""Perfis de energia + EnergyService espelhando energy_service.cpp."""
from dataclasses import dataclass
from analysis import params


@dataclass(frozen=True)
class EnergyProfile:
    name: str
    initial: float
    cost_mqtt: float
    cost_espnow: float
    cost_tick: float
    current_leader_ma: float
    current_member_ma: float
    current_idle_ma: float

    def current_for(self, role: str) -> float:
        if role == "leader":
            return self.current_leader_ma
        if role == "member":
            return self.current_member_ma
        return self.current_idle_ma


ABSTRACT = EnergyProfile(
    name="abstract", initial=params.INITIAL_BUDGET,
    cost_mqtt=params.COST_MQTT, cost_espnow=params.COST_ESPNOW_SEND,
    cost_tick=params.COST_TICK,
    current_leader_ma=float("nan"), current_member_ma=float("nan"),
    current_idle_ma=float("nan"),
)


def calibrated(leader_ma: float, member_ma: float, idle_ma: float,
               capacity_mah: float = params.BATTERY_CAPACITY_MAH) -> EnergyProfile:
    """Perfil em unidades reais. initial em mAh; custos = carga (mAh) por operação.

    Aproxima a carga por operação pela corrente do papel durante uma janela de
    operação típica (LOOP_PERIOD_MS). Ajuste fino quando houver medições de duração
    real de cada burst (Spec 2)."""
    win_h = params.LOOP_PERIOD_MS / 3_600_000.0  # ms -> h
    return EnergyProfile(
        name="calibrated", initial=capacity_mah,
        cost_mqtt=leader_ma * win_h, cost_espnow=member_ma * win_h,
        cost_tick=idle_ma * (params.ENERGY_TICK_PERIOD_MS / 3_600_000.0),
        current_leader_ma=leader_ma, current_member_ma=member_ma,
        current_idle_ma=idle_ma,
    )


class EnergyService:
    """Espelha energy_service.cpp (residual local + tabela de peers com TTL)."""
    def __init__(self, profile: EnergyProfile):
        self.profile = profile
        self.residual = float(profile.initial)
        self._tick_last = 0
        self._peers: dict[bytes, tuple[float, int]] = {}

    def _dec(self, cost: float) -> None:
        self.residual = 0.0 if self.residual <= cost else self.residual - cost

    def on_mqtt_publish(self) -> None:
        self._dec(self.profile.cost_mqtt)

    def on_espnow_send(self) -> None:
        self._dec(self.profile.cost_espnow)

    def tick(self, now_ms: int) -> None:
        if now_ms - self._tick_last < params.ENERGY_TICK_PERIOD_MS:
            return
        self._tick_last = now_ms
        self._dec(self.profile.cost_tick)

    def get_residual(self) -> float:
        return self.residual

    def on_peer_energy(self, mac: bytes, residual: float, now_ms: int) -> None:
        self._peers[mac] = (residual, now_ms)

    def get_peer_residual(self, mac: bytes, now_ms: int) -> tuple[float, bool]:
        sample = self._peers.get(mac)
        if sample is None or (now_ms - sample[1]) >= params.PEER_TTL_MS:
            return (0.0, False)
        return (sample[0], True)
