# Independent `pile-simulator`

This directory contains the cloud-side deterministic proposal producer. It
does not import Qt, open SQLite, or write application data. The B server remains
the only business-state writer and must authenticate and validate every
proposal.

## Repository status

The checked-in Python process implements the deterministic planner, a
SQLite-free in-memory `SimulatorCluster`, a persistent development gateway
loop, and the one-shot stdin proposal mode. The loop registers on the existing
server TCP port, receives the server-owned snapshot, accepts server commands,
returns command ACKs, and submits periodic simulation ticks. The server's
existing user/admin requests remain unchanged; only simulator message types
are added to the same dispatcher. This is a demo-oriented loop, not a
production mTLS/metrics deployment.

For a local contract-vector run:

```sh
cd services/pile-simulator
python3 -m unittest -v
printf '%s\n' '{"stations":[{"station_id":42,"snapshot_version":7,"seed_id":"demo-2026-09","status":"active","piles":[{"pile_id":4201,"status":"idle","simulated":true,"active_order":false}]}]}' \
  | python3 pile_simulator.py --tick-id 1842
```

For a resident local cluster (the server still owns SQLite):

```sh
python3 pile_simulator.py --gateway-host 127.0.0.1 --gateway-port 45454 \
  --simulator-id pile-simulator-dev-1 --seed-id demo-2026-09 --interval-seconds 2
```

The simulator never receives a database path. A lost TCP connection is retried
with a fresh registration and snapshot. The current demo loop keeps the
business path intentionally small; long-term mTLS, metrics and deployment
hardening are outside this implementation.
