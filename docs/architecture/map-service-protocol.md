# Server-side Tencent Maps and pile simulation contract

## Status and relationship to the current mainline

This document is the corrected extension of Socket Protocol v1 proposed by
PR #14. Schema v0.4, deterministic generation, map-only cache/audit
persistence, the three public handlers on the explicit server-mock path, and
the internal simulator proposal boundary are implemented. The deterministic
server-mock path and production
Tencent HTTP adapter is now wired to the same handlers and uses Tencent's
official `nearby(...)` boundary syntax for place search. Event-loop worker
isolation, cache-miss coalescing, and the private mTLS simulator transport
remain deployment work.

The existing user, order, wallet, administrator, and `admin.pile.list`
contracts remain authoritative. PR #13 has now been merged to `main`
(`f3bee707`), so `admin.pile.list` is an authenticated, cursor-paged inventory
read with the 1 MiB response guard. It never starts a map synchronization or a
pile simulation tick.

## 1. Boundary and ownership

```text
Qt clients
    | public Socket Protocol v1
    v
server map service / database service
    |-- Tencent WebService (server-only credential)
    |-- map cache and audit stores
    |-- station upsert and pile generation
    |-- pile simulation gateway
    `-- SQLite (the only business-state writer)

Independent cloud pile-simulator
    | authenticated internal simulator channel
    `-- proposes deterministic pile-state changes; never writes SQLite
```

- Tencent credentials exist only in server-side environment or ignored local
  configuration. They never appear in a client response, database row, log,
  cache key, or Git-tracked file.
- The server is the only client-visible map-data authority: POI search,
  route planning, and station search are server-side Tencent calls, and their
  results reach clients only through the Socket protocol as validated map
  data. Clients never call Tencent for map data and never load
  credential-bearing Tencent JavaScript; they draw returned geometry locally.
- Base-map rendering (static imagery / SDK / tiles) is a client-side concern
  and is outside the server map service scope: only map-data and business
  payloads travel through the Socket protocol, and the server map service
  never proxies base-map imagery or tiles.
- Base-map exception, admin client only, display-only: the admin Qt client
  renders Tencent static-map imagery directly with a dedicated deployment
  key (`TENCENT_STATIC_MAP_KEY`, WebService type). The exception is bounded:
  - the key feeds the static-map image request only and must never be used
    against business map-data APIs (POI search, geocoding, route planning,
    and the like), which remain server-side;
  - the user client holds no Tencent credential of any kind;
  - the key is deployment configuration and must never enter Git, logs,
    screenshots, or the database, and credential-bearing URLs are built in
    memory only;
  - the exception is not a map-data channel: station coordinates and
    business state still arrive over the Socket protocol, and the server
    remains the only authority for map data.
- The server remains the only authority for users, stations, piles, orders,
  wallet state, and lifecycle transitions.
- The independent simulator is an untrusted proposal producer. The server
  re-checks every proposal against current pile/order state in a transaction.

## 2. Common map objects

### 2.1 `origin`

An origin is exactly one of:

```json
{"kind":"address","value":"沈阳市浑南区软件园"}
```

```json
{"kind":"coordinate","latitude":41.7192,"longitude":123.4315}
```

Rules:

- `kind` is `address` or `coordinate`.
- An address is trimmed, Unicode-NFC-normalized, and 1–200 characters.
- Latitude is finite and in `[-90, 90]`; longitude is finite and in
  `[-180, 180]`.
- Address and coordinate fields cannot be mixed.
- Cache normalization rounds coordinates to five decimal places. The
  normalized value is used for cache keys and audit; raw user coordinates are
  not retained as long-term history.

### 2.2 Data source and warning

`data_source` is one of:

- `tencent_live`: the required Tencent calls succeeded;
- `tencent_cache`: an unexpired parsed cache entry was used;
- `tencent_stale`: Tencent failed and a cache entry not older than seven days
  was used;
- `server_mock`: an explicitly enabled deterministic server demo source was
  used.

`warning` is either JSON `null` or:

```json
{
  "code": 1403,
  "name": "MAP_UPSTREAM_UNAVAILABLE",
  "message": "地图服务暂不可用，当前展示缓存结果",
  "retryable": true,
  "degraded": true
}
```

`tencent_stale` and `server_mock` must include a non-null warning. Clients
branch on `code`, not on `message`, and must label degraded data as non-live.
Code `1410` is reserved for the `server_mock` warning and is never emitted as
a top-level failure response.

## 3. `map.station.search`

### Request

```json
{
  "v":1,
  "id":"map-search-001",
  "type":"map.station.search",
  "payload":{
    "user_id":1,
    "origin":{"kind":"address","value":"沈阳市浑南区软件园"},
    "radius_meters":1000,
    "page_size":20,
    "page_token":null
  }
}
```

Rules:

- `user_id` is a positive existing user. Frozen users may perform this read.
- `radius_meters` is an integer in `10..1000`, default `1000`.
- `page_size` is an integer in `1..20`, default `20`.
- `page_token` is either JSON `null` or an opaque server token of at most 256
  characters. It is not interpreted by clients. The token is signed or
  server-side stateful, is bound to the normalized query/filter, and carries
  the provider continuation position; clients must return it unchanged.
- The search keyword is fixed by the server to charging stations; clients
  cannot submit an arbitrary Tencent keyword.
- The request fingerprint includes the normalized origin, radius, page size,
  page token, user ID, and operation. Reusing an ID with a different
  fingerprint returns `CONFLICT`.

### Successful response

```json
{
  "v":1,
  "id":"map-search-001",
  "type":"map.station.search.result",
  "payload":{
    "provider":"tencent",
    "data_source":"tencent_live",
    "resolved_origin":{"latitude":41.7192,"longitude":123.4315},
    "stations":[
      {
        "id":42,
        "provider":"tencent",
        "provider_poi_id":"provider-poi-id",
        "name":"示例充电站",
        "address":"沈阳市浑南区示例路1号",
        "latitude":41.721,
        "longitude":123.435,
        "distance_meters":480,
        "status":"active",
        "pile_total":8,
        "pile_idle":6,
        "pile_reserved":0,
        "pile_charging":0,
        "pile_fault":1,
        "pile_offline":1,
        "map_synced_at":"2026-09-07T08:00:00Z",
        "pile_snapshot_version":3
      }
    ],
    "has_more":false,
    "next_page_token":null,
    "fetched_at":"2026-09-07T08:00:00Z",
    "expires_at":"2026-09-08T08:00:00Z",
    "map_request_log_id":10001,
    "warning":null
  }
}
```

`map_request_log_id` is an audit-log identifier. It is deliberately different
from `request_records`, which stores idempotent successful responses.

The server must:

1. resolve an address when necessary and validate the resulting coordinate;
2. query Tencent and validate every returned POI;
3. upsert by `(provider, provider_poi_id)`;
4. generate piles only when the station has no piles;
5. write the station, first-generation piles, map audit row, and
   `request_records` row in one database transaction;
6. return the exact stored response when the same request ID and fingerprint
   are replayed.

The response is the first page when `page_token` is null. The server must
return `has_more=true` and an opaque `next_page_token` when more valid POIs
exist. Results are ordered by `distance_meters ASC, provider_poi_id ASC` for
stable continuation. It may reduce the requested page size to fit the frame
limit.

### Cache and live business snapshot rule

Station-search cache entries contain only external map data: normalized query,
resolved origin, provider POI identity, name, address, coordinates, distance,
provider continuation, fetch/expiry metadata, and sanitized upstream outcome.
They must not contain `status`, any `pile_*` count, pile price/power, or
`pile_snapshot_version`.

For every ordinary live, fresh-cache, stale-cache, or server-mock response, the
server resolves the returned internal station IDs and re-aggregates station
status, pile counts, and snapshot versions from SQLite in the response
transaction immediately before serialization. A cache hit therefore reuses
only map data and never reuses a prior business snapshot. The only exception
is a successful `map.station.search` request-ID replay: it returns the exact
stored `request_records` response by design, including its historical
snapshot. It performs no new upstream request, map-cache read, or business
aggregation.

## 4. `map.route.plan`

### Request

```json
{
  "v":1,
  "id":"map-route-001",
  "type":"map.route.plan",
  "payload":{
    "user_id":1,
    "origin":{"kind":"coordinate","latitude":41.7192,"longitude":123.4315},
    "station_id":42,
    "mode":"driving"
  }
}
```

- `user_id` is a positive existing user; frozen users may perform this read.
- `mode` is `driving` or `walking`.
- `station_id` must identify a station with valid coordinates. The server
  reads the destination coordinates; arbitrary client destination coordinates
  are rejected. The response retains the station's current status; route
  availability for inactive stations is a deployment policy and must not
  silently enable charging or reservation.

The result contains the provider, data source, mode, normalized origin,
destination station, distance in meters, duration in seconds, decoded
`[latitude, longitude]` polyline, fetch/expiry timestamps,
`map_request_log_id`, and `warning`.

The server decodes Tencent's compressed polyline and returns at most 4096
points. It must preserve the first and last point and progressively decimate
the line until the complete compact JSON envelope fits `kMaxPayloadBytes`.
If two points and the required envelope still cannot fit, return
`MAP_RESPONSE_TOO_LARGE` (1409), not an invalid frame.

Route planning is read-only and does not create a `request_records` replay
row. Every attempt, including cache hits and failures, creates a sanitized
`map_request_logs` row.

## 5. `admin.map.audit.list`

This is an authenticated read. The request must include the existing admin
session `token`; no `administrator_id` is required for reads.

```json
{
  "v":1,
  "id":"map-audit-001",
  "type":"admin.map.audit.list",
  "payload":{
    "token":"administrator-session-token",
    "operation":"map.route.plan",
    "result_status":"success",
    "page_token":null,
    "limit":50
  }
}
```

`operation` and `result_status` are optional filters. `limit` is `1..100`,
default `50`. Results are ordered by `created_at DESC, id DESC`. The page
token is bound to the filters and last sort position. The response contains
`records`, `has_more`, and an opaque `next_page_token`.

The server must apply the same complete-envelope size check used by every
other response and reduce the page when necessary. A single record that cannot
fit returns 1409.

## 6. Pile response compatibility

The existing `pile.list` request is unchanged. New fields are additive and
old clients must ignore them. For v0.3 responses where the fields do not yet
exist, clients use these defaults:

| Field | Missing-value meaning |
|---|---|
| `snapshot_version` | `0`, meaning no server snapshot contract is available |
| `generated_at` | `null` |
| `refresh_after_seconds` | `null`, no polling promise |
| `simulated` | `false` |
| `status_source` | `seed` for legacy seed rows, otherwise `business` |
| `status_updated_at` | `updated_at` |

`admin.pile.list` uses the same per-pile fields and keeps its existing
`after_id` / `next_after_id` pagination and 1 MiB protection. It does not
return a single cross-station snapshot version; consumers use each pile's
station metadata or refresh the station-specific `pile.list` view.

## 7. Station and pile generation

First import and generation occur in one `BEGIN IMMEDIATE` transaction.

- A station receives 4–12 piles.
- The deterministic input and generator are fixed by the algorithm below;
  implementations may not substitute a language runtime random generator.
- The generator chooses a pile count first, then assigns exactly the rounded
  `60%` fast-pile target, clamped to leave at least one fast and one slow pile.
- Fast power is one of 60, 120, or 180 kW; slow power is one of 7, 11, or
  22 kW.
- Base prices are 90, 110, or 130 fen/kWh; fast piles add 20 fen/kWh.
- Initial statuses are `idle`, `fault`, or `offline` with deterministic 80/10/10
  weights. An active station must end with at least one idle pile.
- Codes are `M<station_id>-<two-digit sequence>` and are unique per station.
- An existing station with at least one pile is never regenerated, even if the
  configured seed changes.

### Deterministic generator algorithm and vectors

All text is UTF-8 after Unicode NFC normalization. Let `M` be
`seed_id + 0x00 + provider + 0x00 + provider_poi_id`; `H = SHA-256(M)` is the
32-byte digest. Interpret `H[0..7]` and `H[8..15]` as unsigned 64-bit
little-endian integers `initstate` and `initseq`.

Use PCG XSH RR 64/32 with unsigned wraparound modulo `2^64`:

```text
state = 0
inc = (initseq << 1) | 1
next32(); state = state + initstate; next32()

next32():
  old = state
  state = old * 6364136223846793005 + inc mod 2^64
  xorshifted = (((old >> 18) xor old) >> 27) & 0xffffffff
  rot = old >> 59
  return ((xorshifted >> rot) | (xorshifted << ((-rot) & 31))) & 0xffffffff

below(n):
  threshold = (2^32 - n) mod n
  repeat r = next32() until r >= threshold
  return r mod n
```

Generate piles in sequence order without shuffling: `count = 4 + below(9)`;
`fast_count = clamp(round_half_up(count * 60 / 100), 1, count - 1)`. For each
sequence `1..count`, choose type by whether it is within `fast_count`, then
consume exactly three `below` values in this order: power index (`3` choices),
base-price index (`3` choices), status bucket (`10` choices: `0..7=idle`,
`8=fault`, `9=offline`). Fast piles add 20 fen/kWh. If the resulting active
station has no idle pile, replace the final pile status with `idle` without
consuming another random value.

The generator implementation must contain these test vectors; rows list
`type/power_kw/price_fen/status` in sequence order:

| `seed_id`, provider, POI | SHA-256 | count / fast | expected piles |
|---|---|---:|---|
| `demo-2026-09`, `tencent`, `poi-001` | `4387d8ad9230ac66c58ece8cad123e5bcf9cad2a225a3436c35d471325b029c2` | 4 / 2 | `fast/60/130/idle`, `fast/120/130/fault`, `slow/11/130/idle`, `slow/11/130/idle` |
| `demo-2026-09`, `tencent`, `poi-002` | `470d337567a7fd7e862871d0473d4f50f163e870fe29ac0a68636e4f365cf23e` | 4 / 2 | `fast/180/150/idle`, `fast/60/130/idle`, `slow/22/130/idle`, `slow/11/90/idle` |
| `test-seed`, `tencent`, `shenyang-42` | `f91b6b81a4b211b546bdfeadc6f4e97ad08d992bc7ad44a0a3e26c1c3dde9675` | 7 / 4 | `fast/60/110/idle`, `fast/180/150/idle`, `fast/120/110/idle`, `fast/60/130/idle`, `slow/11/130/offline`, `slow/7/130/offline`, `slow/22/110/idle` |

## 8. Simulation state machine

Only `simulated=true` piles without unfinished orders are eligible. The
simulator may change only among `idle`, `fault`, and `offline`; it never writes
`reserved` or `charging`.

The eligible-order exclusion set is:

```text
pending_reservation, reserved, charging, pending_settlement
```

Every tick uses one `BEGIN IMMEDIATE` transaction in the authoritative server
database. It selects eligible piles in ascending `pile_id`, computes the full
post-batch state, and rejects any batch that would leave an active station
below `EV_PILE_SIMULATION_MIN_IDLE` (default `1`). The minimum may be set to
zero only in an explicitly labelled test/demo environment.

Business transactions and administrator restart transactions win over a
simulation proposal. A stale proposal is rejected when its expected station
snapshot version no longer matches. Retry handling is defined in the cloud
gateway contract below.
Successful ticks increment each changed station snapshot once and append one
`pile_status_events` row per changed pile. No-op ticks do not increment the
version.

For cloud proposals, each eligible pile consumes one deterministic `below(100)`
draw from a PCG32 stream seeded by `seed_id + 0x00 + decimal(tick_id) + 0x00 +
decimal(pile_id)` (same SHA-256/PCG rules as the generation algorithm). The
transition buckets are: `idle`: 0–89 idle, 90–94 fault, 95–99 offline;
`fault`: 0–59 fault, 60–94 idle, 95–99 offline; `offline`: 0–59 offline,
60–94 idle, 95–99 fault. Piles are evaluated in ascending ID order and the
minimum-idle rule is applied after the complete batch. These rules are the
deterministic state-transition contract, not merely an implementation hint.

Tick test vectors (before minimum-idle filtering) are:

| seed, tick, pile, current | SHA-256 | draw | proposed state |
|---|---|---:|---|
| `demo-2026-09`, 1842, 4201, `idle` | `7ac28b1569f714d07a0a46696656a499cb866a508cb5546dc13a5fe4f0eff403` | 58 | `idle` |
| `demo-2026-09`, 1842, 4202, `fault` | `d4630da054e8d3400cc72680c568c98514e8b496b2bd6fd7559f795c1ce7b95b` | 34 | `fault` |
| `test-seed`, 7, 101, `offline` | `2284bd68114105e4c6326790a372471e4fdcaddfb272dfa95ff53a0dc775825a` | 39 | `offline` |

## 9. Independent cloud simulator contract

The production simulator runs as a separate cloud service named
`pile-simulator`. A local in-process simulator may be used for development, but
both use the same gateway semantics.

The simulator communicates with a private authenticated internal channel; it
does not expose the public client Socket port and never opens the SQLite file.
The transport may be mTLS TCP or an equivalent private service channel, but
the logical message is fixed:

```json
{
  "simulator_id":"pile-simulator-prod-1",
  "seed_id":"demo-2026-09",
  "tick_id":1842,
  "expected_versions":{"42":7},
  "changes":[
    {"pile_id":4201,"from":"idle","to":"fault","reason":"simulation"}
  ]
}
```

Gateway rules:

- `simulator_id`, `tick_id` is an idempotency key. Replaying an accepted tick,
  or retrying after the sender lost its response, returns the original result
  without applying it twice.
- The server checks the seed, expected station versions, current pile state,
  active orders, `simulated` flag, and minimum-idle invariant again.
- Accepted changes are committed by the server in the same transaction as
  snapshot/version/event updates.
- A stale/version conflict changes nothing. Once the simulator receives that
  conflict, it reads a new snapshot, recomputes the proposal, and uses a new
  `tick_id`; the old ID remains a permanent record of the rejected proposal.
- A permanent business rejection (for example invalid service identity, a
  non-simulated pile, an active order, or a prohibited transition) is not
  automatically retried. A transient transport failure before any response is
  retried unchanged with the same `tick_id`.
- The gateway authenticates the simulator with mTLS/service identity,
  authorizes only the simulation operation, rate-limits ticks, and records
  heartbeat/last-seen data. Credentials are not stored in Git.
- If the cloud simulator is unavailable, piles retain their last server state;
  reservations, charging, restart, and settlement continue to work.

The cloud service owns scheduling, deterministic proposal calculation, and
health reporting. The B server owns authorization, validation, persistence,
and all business-state transitions.

## 10. Persistence and retention

Schema v0.4 adds the following fields/tables. The migration must be atomic and
must preserve all valid v0.3 data.

- `stations`: `provider TEXT NOT NULL DEFAULT 'internal'`,
  `provider_poi_id TEXT`, `map_synced_at TEXT`, `map_content_hash TEXT`;
  unique `(provider, provider_poi_id)` when the provider ID is non-null.
- `charging_piles`: `simulated INTEGER NOT NULL DEFAULT 0`,
  `status_source TEXT NOT NULL DEFAULT 'seed'`,
  `status_updated_at TEXT NOT NULL`.
- `map_request_logs`: sanitized client request/result records.
- `map_upstream_call_logs`: one row per geocode, POI, detail, driving, or
  walking upstream call.
- `map_cache_entries`: normalized cache key, operation, parsed payload,
  `expires_at`, and `stale_until`; never a credential-bearing URL.
- `pile_status_events`: from/to status, source, reason, tick ID, and timestamp.
- `simulation_state`: one row per station containing seed ID, tick ID,
  last-run time, and snapshot version.

Map request logs, upstream logs, cache entries, and pile-status events are
retained for 30 UTC days. Cleanup must not delete stations, piles, orders, or
wallet records. Long-term precise user locations, raw Tencent JSON, complete
URLs, and credentials are forbidden.

## 11. Tencent HTTP adapter

When `EV_MAP_SERVER_MOCK` is not `1`, `MapService` selects the production
`HttpTencentClient`. It requires `TENCENT_MAP_ENABLED=1` and a non-empty
`TENCENT_MAP_KEY`; the key is added to HTTPS query parameters at runtime and
is never written to logs, audit rows, cache entries, or responses. The default
base URL is `https://apis.map.qq.com`. `TENCENT_MAP_BASE_URL` is available only
for loopback fake HTTP tests. Requests use Qt `QUrlQuery` and a bounded timeout
(`TENCENT_MAP_TIMEOUT_MS`, default 5000 ms).

The adapter calls `/ws/geocoder/v1/`, `/ws/place/v1/search`,
`/ws/direction/v1/driving/`, or `/ws/direction/v1/walking/`. Tencent `status`
and HTTP failures are mapped to the canonical map error codes: timeout,
unavailable, quota, permission, no result, or invalid response. Route distance
is metres; Tencent route duration (minutes) is converted to protocol seconds.
Tencent's compressed polyline is decoded from `[lat0,lng0,deltaLat1,deltaLng1,...]`,
with coordinate/range validation and a maximum of 4096 retained points.

## 12. Configuration and non-functional requirements

```text
TENCENT_MAP_KEY=<server-only local secret>
TENCENT_MAP_ENABLED=1
EV_MAP_STATION_CACHE_TTL_SECONDS=86400
EV_MAP_ROUTE_CACHE_TTL_SECONDS=300
EV_MAP_STALE_MAX_SECONDS=604800
EV_PILE_SIMULATION_ENABLED=1
EV_PILE_SIMULATION_INTERVAL_SECONDS=60
EV_PILE_SIMULATION_SEED=<server/cloud-secret-free seed identifier>
EV_PILE_SIMULATION_MIN_IDLE=1
```

- HTTP and slow database work must not block the Socket event loop.
- Concurrent cache misses for the same normalized key should be coalesced.
- Every successful or error response is compact-serialized and checked against
  `kMaxPayloadBytes` before `encodeFrame()`.
- If a page can be reduced, the server returns a smaller page and a cursor. If
  one logical item cannot fit, it returns 1409 rather than writing an invalid
  frame.
