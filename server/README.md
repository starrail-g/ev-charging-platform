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
`var/ev-charging.db`), and `EV_SCHEMA_PATH` optionally points to the schema
SQL (the repository schema is located automatically for normal source/build
tree launches). The first connection initializes an empty database with
`database/schema/schema.sql` and requires schema version `0.2`.

The service provides `health`, `echo`, `user.login`, station/pile queries,
active-order lookup, reservation transitions, and charging
start/stop/settlement. Login accepts an 11-digit ASCII phone number, reads an
existing user, or atomically registers a new active user with zero balance.
See
`docs/architecture/protocol.md` for framing and the v1 contract.

With the server running, validate the basic TCP path:

```bash
python3 server/tests/smoke.py
```

When using a non-default port, pass the same environment setting to both
commands, for example `EV_SERVER_PORT=45455`.

For a clean login smoke run, use a temporary database and explicit schema:

```bash
EV_DATABASE_PATH=/tmp/ev-charging-demo.db \
EV_SCHEMA_PATH=database/schema/schema.sql \
EV_SERVER_PORT=45455 ./ev-server
EV_SERVER_PORT=45455 python3 server/tests/smoke.py
```
