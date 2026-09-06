# EV Server

The initial Qt TCP server implements protocol v1 framing and diagnostic
requests. The application listens on `127.0.0.1:45454` by default.

Build with the installed Qt 6 qmake:

```bash
mkdir -p build/server
cd build/server
qmake6 ../../server/server.pro
make
./ev-server
```

`EV_SERVER_HOST` and `EV_SERVER_PORT` override the bind host and port for
local development. `EV_DATABASE_PATH` selects the SQLite file (default:
`var/ev-charging.db`), `EV_SCHEMA_PATH` optionally points to the schema SQL,
and `EV_DATABASE_SEED_PATH` optionally points to repeatable development seed
data. The repository schema is located automatically for normal source/build
tree launches. The first connection initializes an empty database with
`database/schema/schema.sql` and requires schema version `0.3`; when
`EV_DATABASE_SEED_PATH` is set, the seed is loaded only during that initial
creation. Existing databases are never reseeded automatically.

The service provides `health`, `echo`, `user.login`, profile read/update,
wallet recharge, station/pile queries, active and historical order lookup,
reservation transitions, and charging start/stop/settlement, plus administrator
login, statistics, station/pile, and user management operations. Login accepts
an 11-digit ASCII phone number, reads an existing user, or atomically registers
a new active user with zero balance. `admin.login` returns an 8-hour
process-local session token; every other `admin.*` request must carry that
token, including read-only queries. Mutation requests additionally carry
`administrator_id`, which must match the authenticated token subject. See
`docs/architecture/protocol.md` for framing and the v1 contract.

With the server running, validate the basic TCP path:

```bash
python3 server/tests/smoke.py
python3 server/tests/concurrency.py
```

When using a non-default port, pass the same environment setting to both
commands, for example `EV_SERVER_PORT=45455`.

For a clean end-to-end smoke run, use a temporary database, explicit schema,
and deterministic development seed data:

```bash
EV_DATABASE_PATH=/tmp/ev-charging-demo.db \
EV_SCHEMA_PATH=database/schema/schema.sql \
EV_DATABASE_SEED_PATH=database/seeds/dev.sql \
EV_SERVER_PORT=45455 ./ev-server
EV_SERVER_PORT=45455 python3 server/tests/smoke.py
```

To initialize a database manually instead, run the schema and seed scripts in
order. The seed is optional when an empty production-like database is desired:

```bash
mkdir -p var
sqlite3 var/ev-charging.db < database/schema/schema.sql
sqlite3 var/ev-charging.db < database/seeds/dev.sql
```
