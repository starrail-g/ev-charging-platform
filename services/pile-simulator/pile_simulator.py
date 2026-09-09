#!/usr/bin/env python3
"""Deterministic, SQLite-free cloud pile simulator.

The process owns scheduling and proposal calculation only.  It receives a
snapshot from the authenticated B gateway (or a JSON snapshot on stdin for a
local demo), then submits a proposal.  It never opens the application SQLite
database.  The gateway remains responsible for all validation and writes.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import socket
import struct
import sys
import time
import uuid
import unicodedata
from dataclasses import dataclass
from typing import Iterable, Mapping


def normalized(value: str) -> str:
    return unicodedata.normalize("NFC", value)


def input_bytes(*parts: object) -> bytes:
    return b"\0".join(normalized(str(part)).encode("utf-8") for part in parts)


class Pcg32:
    """PCG XSH RR 64/32 with the contract's little-endian initialization."""

    def __init__(self, seed: bytes):
        digest = hashlib.sha256(seed).digest()
        init_state = int.from_bytes(digest[0:8], "little")
        init_seq = int.from_bytes(digest[8:16], "little")
        self.state = 0
        self.increment = ((init_seq << 1) | 1) & ((1 << 64) - 1)
        self.next32()
        self.state = (self.state + init_state) & ((1 << 64) - 1)
        self.next32()

    def next32(self) -> int:
        old = self.state
        self.state = (old * 6364136223846793005 + self.increment) & ((1 << 64) - 1)
        xorshifted = (((old >> 18) ^ old) >> 27) & 0xFFFFFFFF
        rotation = old >> 59
        return ((xorshifted >> rotation) | (xorshifted << ((-rotation) & 31))) & 0xFFFFFFFF

    def below(self, n: int) -> int:
        if n <= 0:
            raise ValueError("n must be positive")
        threshold = ((1 << 32) - n) % n
        while True:
            value = self.next32()
            if value >= threshold:
                return value % n


@dataclass(frozen=True)
class GeneratedPile:
    pile_type: str
    power_kw: int
    price_fen_per_kwh: int
    status: str


def generator_digest(seed_id: str, provider: str, provider_poi_id: str) -> str:
    return hashlib.sha256(input_bytes(seed_id, provider, provider_poi_id)).hexdigest()


def generate_piles(seed_id: str, provider: str, provider_poi_id: str,
                   active_station: bool = True) -> list[GeneratedPile]:
    random = Pcg32(input_bytes(seed_id, provider, provider_poi_id))
    count = 4 + random.below(9)
    fast_count = max(1, min(count - 1, (count * 60 + 50) // 100))
    fast_power = (60, 120, 180)
    slow_power = (7, 11, 22)
    base_prices = (90, 110, 130)
    result: list[GeneratedPile] = []
    has_idle = False
    for sequence in range(1, count + 1):
        fast = sequence <= fast_count
        power_index = random.below(3)
        price_index = random.below(3)
        status_bucket = random.below(10)
        status = "idle" if status_bucket <= 7 else "fault" if status_bucket == 8 else "offline"
        has_idle = has_idle or status == "idle"
        result.append(GeneratedPile(
            "fast" if fast else "slow",
            (fast_power if fast else slow_power)[power_index],
            base_prices[price_index] + (20 if fast else 0),
            status,
        ))
    if active_station and result and not has_idle:
        result[-1] = GeneratedPile(result[-1].pile_type, result[-1].power_kw,
                                    result[-1].price_fen_per_kwh, "idle")
    return result


def transition(seed_id: str, tick_id: int, pile_id: int, current: str) -> tuple[int, str]:
    draw = Pcg32(input_bytes(seed_id, tick_id, pile_id)).below(100)
    if current == "idle":
        proposed = "idle" if draw < 90 else "fault" if draw < 95 else "offline"
    elif current == "fault":
        proposed = "fault" if draw < 60 else "idle" if draw < 95 else "offline"
    elif current == "offline":
        proposed = "offline" if draw < 60 else "idle" if draw < 95 else "fault"
    else:
        proposed = current
    return draw, proposed


@dataclass(frozen=True)
class PileSnapshot:
    pile_id: int
    station_id: int
    status: str
    simulated: bool
    active_order: bool


@dataclass(frozen=True)
class StationSnapshot:
    station_id: int
    version: int
    seed_id: str
    status: str
    piles: tuple[PileSnapshot, ...]


class DeterministicPlanner:
    def __init__(self, simulator_id: str, seed_id: str):
        self.simulator_id = simulator_id
        self.seed_id = seed_id

    def proposal(self, tick_id: int, stations: Iterable[StationSnapshot]) -> dict:
        expected_versions: dict[str, int] = {}
        changes: list[dict] = []
        for station in sorted(stations, key=lambda item: item.station_id):
            if station.seed_id != self.seed_id or station.status != "active":
                continue
            expected_versions[str(station.station_id)] = station.version
            for pile in sorted(station.piles, key=lambda item: item.pile_id):
                if not pile.simulated or pile.active_order or pile.status not in {"idle", "fault", "offline"}:
                    continue
                _, proposed = transition(self.seed_id, tick_id, pile.pile_id, pile.status)
                if proposed != pile.status:
                    changes.append({
                        "pile_id": pile.pile_id,
                        "from": pile.status,
                        "to": proposed,
                        "reason": "simulation",
                    })
        return {
            "simulator_id": self.simulator_id,
            "seed_id": self.seed_id,
            "tick_id": tick_id,
            "expected_versions": expected_versions,
            "changes": changes,
        }


class LengthPrefixedGateway:
    """Minimal authenticated internal channel client.

    Authentication is delegated to the private transport (mTLS in production).
    A bearer value can be supplied for a development gateway, but it is never
    persisted by this service.
    """

    def __init__(self, host: str, port: int, timeout: float = 3.0):
        self.host = host
        self.port = port
        self.timeout = timeout

    def request(self, message: Mapping) -> dict:
        body = json.dumps(message, separators=(",", ":")).encode("utf-8")
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.sendall(struct.pack(">I", len(body)) + body)
            header = _recv_exact(sock, 4)
            size = struct.unpack(">I", header)[0]
            if size == 0 or size > 1024 * 1024:
                raise RuntimeError("gateway returned an invalid frame")
            return json.loads(_recv_exact(sock, size).decode("utf-8"))


@dataclass
class PileRuntime:
    pile_id: int
    station_id: int
    status: str
    simulated: bool
    active_order: bool = False
    order_id: int | None = None


class SimulatorCluster:
    """In-memory cluster runtime. The server remains the state authority."""

    def __init__(self, simulator_id: str, seed_id: str):
        self.simulator_id = simulator_id
        self.seed_id = seed_id
        self.stations: list[StationSnapshot] = []
        self._piles: dict[int, PileRuntime] = {}
        # A stable simulator_id may reconnect after old tick ids have already
        # been persisted. Milliseconds keep a restarted demo cluster moving
        # forward instead of colliding with tick 1, 2, ... from the prior run.
        self._tick_id = int(time.time() * 1000)

    def register_snapshot(self, snapshot: Mapping) -> None:
        self.stations = _snapshot_from_json(snapshot)
        self._piles = {
            pile.pile_id: PileRuntime(pile.pile_id, pile.station_id, pile.status,
                                      pile.simulated, pile.active_order)
            for station in self.stations for pile in station.piles
        }

    def apply_command(self, payload: Mapping) -> dict:
        pile_id = int(payload.get("pile_id", 0))
        pile = self._piles.get(pile_id)
        command = str(payload.get("command", ""))
        accepted = pile is not None
        if accepted:
            if command == "reserve" and pile.status in {"idle", "reserved"}:
                pile.status, pile.active_order = "reserved", True
            elif command == "release" and pile.status in {"idle", "reserved"}:
                pile.status, pile.active_order, pile.order_id = "idle", False, None
            elif command == "start_charging" and pile.status in {"idle", "reserved", "charging"}:
                pile.status, pile.active_order = "charging", True
            elif command == "stop_charging" and pile.status in {"charging", "idle"}:
                # The server order remains pending_settlement after stop.
                pile.status, pile.active_order = "idle", True
            elif command == "settle" and pile.status == "idle":
                pile.active_order, pile.order_id = False, None
            elif command == "restart" and pile.status in {"fault", "offline", "idle"}:
                pile.status = "idle"
            else:
                accepted = False
            if accepted and payload.get("order_id") is not None:
                pile.order_id = int(payload["order_id"])
        return {"command_id": payload.get("command_id"), "simulator_id": self.simulator_id,
                "pile_id": pile_id, "accepted": accepted,
                "status": pile.status if pile else "unknown"}

    def build_tick(self) -> dict:
        self._tick_id += 1
        stations = []
        for station in self.stations:
            piles = tuple(PileSnapshot(p.pile_id, p.station_id, p.status, p.simulated,
                                       p.active_order) for p in self._piles.values()
                          if p.station_id == station.station_id)
            stations.append(StationSnapshot(station.station_id, station.version,
                                            station.seed_id, station.status, piles))
        return DeterministicPlanner(self.simulator_id, self.seed_id).proposal(self._tick_id, stations)

    def apply_tick_result(self, result: Mapping) -> None:
        for change in result.get("changes", []):
            pile = self._piles.get(int(change["pile_id"]))
            if pile:
                pile.status = str(change["to"])
        versions = result.get("snapshot_versions", {})
        self.stations = [StationSnapshot(s.station_id, int(versions.get(str(s.station_id), s.version)),
                                         s.seed_id, s.status,
                                         tuple(PileSnapshot(p.pile_id, p.station_id, p.status,
                                                            p.simulated, p.active_order)
                                               for p in self._piles.values()
                                               if p.station_id == s.station_id)) for s in self.stations]


class PersistentGateway:
    """Long-lived development connection used by the demo cluster."""

    def __init__(self, host: str, port: int, cluster: SimulatorCluster, interval: float = 2.0):
        self.host, self.port, self.cluster, self.interval = host, port, cluster, interval

    @staticmethod
    def _message(message_type: str, payload: Mapping) -> bytes:
        body = json.dumps({"v": 1, "id": str(uuid.uuid4()), "type": message_type,
                           "payload": payload}, separators=(",", ":")).encode()
        return struct.pack(">I", len(body)) + body

    @staticmethod
    def _read_available(sock: socket.socket, buffer: bytearray) -> list[dict]:
        messages = []
        try:
            data = sock.recv(65536)
            if not data:
                raise ConnectionError("gateway closed the connection")
            buffer.extend(data)
        except socket.timeout:
            return messages
        while len(buffer) >= 4:
            size = struct.unpack(">I", buffer[:4])[0]
            if len(buffer) < 4 + size:
                break
            del buffer[:4]
            messages.append(json.loads(bytes(buffer[:size]).decode()))
            del buffer[:size]
        return messages

    def run(self) -> None:
        while True:
            try:
                with socket.create_connection((self.host, self.port), timeout=3) as sock:
                    sock.settimeout(0.2)
                    sock.sendall(self._message("simulator.register", {"simulator_id": self.cluster.simulator_id}))
                    buffer = bytearray()
                    next_tick = time.monotonic() + self.interval
                    while True:
                        for message in self._read_available(sock, buffer):
                            message_type = message.get("type", "")
                            payload = message.get("payload", {})
                            if message_type == "simulator.register.result":
                                self.cluster.register_snapshot(payload)
                            elif message_type == "simulator.snapshot.result":
                                self.cluster.register_snapshot(payload)
                            elif message_type == "simulator.command":
                                result = self.cluster.apply_command(payload)
                                sock.sendall(self._message("simulator.command.result", result))
                            elif message_type == "simulator.tick.result" and payload.get("accepted"):
                                self.cluster.apply_tick_result(payload)
                            elif message_type == "error" and int(payload.get("code", 0)) == 1201:
                                # A business request may have advanced the
                                # station snapshot. Refresh before the next
                                # deterministic tick instead of replaying a
                                # stale proposal forever.
                                sock.sendall(self._message("simulator.snapshot.get", {}))
                        if time.monotonic() >= next_tick and self.cluster.stations:
                            sock.sendall(self._message("simulator.tick", self.cluster.build_tick()))
                            next_tick = time.monotonic() + self.interval
            except (OSError, ConnectionError, json.JSONDecodeError):
                time.sleep(1.0)


def _recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise ConnectionError("gateway closed the connection")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _snapshot_from_json(value: Mapping) -> list[StationSnapshot]:
    stations: list[StationSnapshot] = []
    for raw_station in value.get("stations", []):
        piles = tuple(
            PileSnapshot(
                int(raw_pile["pile_id"]),
                int(raw_station["station_id"]),
                str(raw_pile["status"]),
                bool(raw_pile.get("simulated", False)),
                bool(raw_pile.get("active_order", False)),
            )
            for raw_pile in raw_station.get("piles", [])
        )
        stations.append(StationSnapshot(
            int(raw_station["station_id"]),
            int(raw_station.get("snapshot_version", 0)),
            str(raw_station["seed_id"]),
            str(raw_station.get("status", "active")),
            piles,
        ))
    return stations


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator-id", default="pile-simulator-dev-1")
    parser.add_argument("--seed-id", default="demo-2026-09")
    parser.add_argument("--tick-id", type=int)
    parser.add_argument("--interval-seconds", type=float, default=2.0)
    parser.add_argument("--snapshot", type=argparse.FileType("r"), default=sys.stdin,
                        help="gateway snapshot JSON; SQLite is never opened")
    parser.add_argument("--gateway-host")
    parser.add_argument("--gateway-port", type=int)
    args = parser.parse_args(argv)

    if args.gateway_host and args.gateway_port and args.tick_id is None:
        cluster = SimulatorCluster(args.simulator_id, args.seed_id)
        PersistentGateway(args.gateway_host, args.gateway_port, cluster,
                          max(0.1, args.interval_seconds)).run()
        return 0
    if args.tick_id is None:
        parser.error("--tick-id is required for one-shot mode")
    snapshot = json.load(args.snapshot)
    planner = DeterministicPlanner(args.simulator_id, args.seed_id)
    proposal = planner.proposal(args.tick_id, _snapshot_from_json(snapshot))
    if args.gateway_host and args.gateway_port:
        response = LengthPrefixedGateway(args.gateway_host, args.gateway_port).request(proposal)
        print(json.dumps(response, ensure_ascii=False, separators=(",", ":")))
    else:
        print(json.dumps(proposal, ensure_ascii=False, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
