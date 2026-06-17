# analysis/simulator/nodes.py
"""Nó do cluster: espelha role_service.cpp + os hooks de energia do
application_controller.cpp (handle_leader/handle_member) e a geração de leituras
do reading_service.cpp."""
from analysis import params
from analysis.simulator import policies
from analysis.simulator.energy import EnergyService
from analysis.simulator.transport import Message

ZERO = policies.ZERO_MAC

class Node:
    def __init__(self, mac, profile, strategy, transport, emit):
        self.mac = mac
        self.profile = profile
        self.strategy = strategy
        self.transport = transport
        self.emit = emit

        self.role = "undecided"
        self.leader_mac = ZERO
        self.energy = EnergyService(profile)
        self.cooldown = policies.CooldownTracker()
        self.known_peers: set[bytes] = set()
        self.alive = True

        self._ping_last = 0
        self._discovery_last = 0
        self._reading_last = 0
        self._term_last = 0
        self._rotate_retry_last = 0
        self._stepping_down = False
        self._rotate_retries_left = 0
        self._pending_next = ZERO

    # ---- helpers ----
    def _sorted_cluster(self):
        return sorted(self.known_peers | {self.mac})

    def _announced_leader(self):
        if self.role == "leader":
            return self._pending_next if self._stepping_down else self.mac
        if self.role == "member":
            return self.leader_mac
        return ZERO

    def _set_leader(self, leader, now_ms):
        self.leader_mac = leader
        self.cooldown.on_became_leader(leader, now_ms)
        if leader == self.mac:
            self.role = "leader"
            self._term_last = now_ms
            self.emit(self, now_ms, "became_leader")
        else:
            self.role = "member"

    # ---- recepção (espelha rx_callbacks) ----
    def _process_inbox(self, now_ms):
        for msg in self.transport.deliver_inbox(self.mac):
            self.known_peers.add(msg.src)  # register_sender_if_new
            if msg.kind == "ping":
                self._on_leader_announced(msg.announced_leader, now_ms)
                self.energy.on_peer_energy(msg.src, msg.residual, now_ms)
            elif msg.kind == "reading":
                if self.role == "leader":
                    self.energy.on_mqtt_publish()  # handle_leader: publica leitura do membro
            elif msg.kind == "rotate":
                self._on_rotate_received(msg.next_leader, now_ms)

    def _on_leader_announced(self, announced, now_ms):
        if announced == ZERO:
            return
        if self.role == "undecided":
            self._set_leader(announced, now_ms)
            return
        if self.role == "member" and self.leader_mac != announced:
            self._set_leader(announced, now_ms)

    def _on_rotate_received(self, nxt, now_ms):
        if self.role != "undecided" and self.leader_mac == nxt:
            return  # idempotente
        self._set_leader(nxt, now_ms)

    # ---- eleição/rotação (espelha role_service::handler) ----
    def _elect(self, now_ms):
        if not self.known_peers:
            return
        smallest = min(self.known_peers | {self.mac})
        self._set_leader(smallest, now_ms)

    def _role_handler(self, now_ms):
        if self.role == "undecided":
            if now_ms - self._discovery_last >= params.DISCOVERY_WINDOW_MS:
                self._elect(now_ms)
                self._discovery_last = now_ms
            return
        if self._stepping_down:
            if now_ms - self._rotate_retry_last < params.ROTATE_RETRY_MS:
                return
            if self._rotate_retries_left > 0:
                self.transport.unicast(self._pending_next,
                                       Message(kind="rotate", src=self.mac,
                                               next_leader=self._pending_next))
                self._rotate_retries_left -= 1
                self._rotate_retry_last = now_ms
            else:
                self._stepping_down = False
                self.leader_mac = self._pending_next
                self.role = "member"
            return
        if self.role != "leader":
            return
        if now_ms - self._term_last < params.TERM_DURATION_MS:
            return
        nxt = policies.pick_next_leader(
            self.strategy, self._sorted_cluster(), self.mac, self.mac,
            residual_of=lambda m: ((self.energy.get_residual(), True) if m == self.mac
                                   else self.energy.get_peer_residual(m, now_ms)),
            cooldown=self.cooldown, now_ms=now_ms)
        if nxt == self.mac:
            self._term_last = now_ms  # política manteve a liderança
            return
        self.emit(self, now_ms, "rotate_sent")
        self.transport.unicast(nxt, Message(kind="rotate", src=self.mac, next_leader=nxt))
        self._pending_next = nxt
        self._rotate_retries_left = params.ROTATE_RETRIES
        self._rotate_retry_last = now_ms
        self._stepping_down = True

    # ---- aplicação (handle_leader/handle_member + reading_service) ----
    def _app_step(self, now_ms):
        new_reading = (now_ms - self._reading_last) >= params.READING_INTERVAL_MS
        if new_reading:
            self._reading_last = now_ms
        if self.role == "leader":
            if new_reading:
                self.energy.on_mqtt_publish()  # publica a própria leitura
        elif self.role == "member" and new_reading:
            self.transport.unicast(self.leader_mac,
                                   Message(kind="reading", src=self.mac,
                                           current_ma=self.profile.current_for("member"),
                                           battery_pct=self._residual_pct()))
            self.energy.on_espnow_send()

    def _residual_pct(self):
        return 100.0 * self.energy.residual / self.profile.initial

    def _maybe_ping(self, now_ms):
        if now_ms - self._ping_last >= params.PING_PERIOD_MS:
            self._ping_last = now_ms
            self.transport.broadcast(self.mac,
                                     Message(kind="ping", src=self.mac,
                                             residual=self.energy.get_residual(),
                                             announced_leader=self._announced_leader()))

    def _check_death(self, now_ms):
        if self.alive and self.energy.residual <= 0:
            self.alive = False
            self.emit(self, now_ms, "node_death")

    # ---- ciclo de 100 ms ----
    def step(self, now_ms):
        if not self.alive:
            return
        self._process_inbox(now_ms)
        self._maybe_ping(now_ms)
        self.energy.tick(now_ms)
        self._role_handler(now_ms)
        self._app_step(now_ms)
        self._check_death(now_ms)
