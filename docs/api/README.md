# API Reference

The Socket API uses the protocol defined in
[`docs/architecture/protocol.md`](../architecture/protocol.md). This directory
will contain endpoint-level examples as database-backed operation handlers
are added. The user and charging lifecycle operations are now available;
the login request is:

```json
{"v":1,"id":"login-1","type":"user.login",
 "payload":{"phone":"13912345678"}}
```

Successful responses return the existing user or create one atomically:

```json
{"v":1,"id":"login-1","type":"user.login.result",
 "payload":{"user":{"id":1,"phone":"13912345678",
 "nickname":"用户5678","avatar_path":null,
 "balance_cents":0,"status":"active"}}}
```

The phone must be exactly 11 ASCII digits. Invalid input returns the shared
`INVALID_REQUEST` (`1002`) error. Database open, initialization, or write
failures return `DATABASE_ERROR` (`1300`). Reservation and charging handlers
use the same error mapping for `NOT_FOUND`, `CONFLICT`, and
`INSUFFICIENT_BALANCE`; their transaction boundaries and state transitions
are defined in `docs/architecture/database.md`. The diagnostic `health` and
`echo` operations remain available.
