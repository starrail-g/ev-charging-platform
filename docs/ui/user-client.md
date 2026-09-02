# User client implementation note

This module is a Qt Widgets baseline for A-S1-02. `UserWindow` owns page transitions and the single `SessionManager`; unauthenticated users are redirected to login and logout clears user/order/selection state.

`MockUserService` is deterministic and in-memory. Its sample station/pile/order data is intentionally separate from UI code. Failure inputs (`timeout`, `error`, `server-error`) make timeout, service and query errors reproducible for review. `IUserService` is the only business boundary; a future B Socket adapter should implement it and map the final protocol enums there.

Navigation currently exposes only the deterministic offline Mock route and a disabled Tencent Maps API placeholder. Real Tencent Maps is intentionally not opened until the API contract is integrated. The key remains local-only and is not logged or rendered.
