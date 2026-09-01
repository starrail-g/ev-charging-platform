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
local development. The service currently provides `health` and `echo`; see
`docs/architecture/protocol.md` for framing and the v1 contract.

With the server running, validate the basic TCP path:

```bash
python3 server/tests/smoke.py
```

When using a non-default port, pass the same environment setting to both
commands, for example `EV_SERVER_PORT=45455`.
