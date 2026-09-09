# Independent `pile-simulator`

This directory contains the cloud-side deterministic proposal producer. It
does not import Qt, open SQLite, or write application data. The B server remains
the only business-state writer and must authenticate and validate every
proposal.

For a local contract-vector run:

```sh
cd services/pile-simulator
python3 -m unittest -v
printf '%s\n' '{"stations":[{"station_id":42,"snapshot_version":7,"seed_id":"demo-2026-09","status":"active","piles":[{"pile_id":4201,"status":"idle","simulated":true,"active_order":false}]}]}' \
  | python3 pile_simulator.py --tick-id 1842
```

Production deployment supplies a private mTLS channel to the gateway. A
development gateway may be selected with `--gateway-host` and
`--gateway-port`; this process still never receives a database path. A lost
response retries the identical proposal and `tick_id`. A structured stale
snapshot response must cause the scheduler to fetch a new snapshot, recompute,
and use a new `tick_id`; permanent business rejections are not retried.
