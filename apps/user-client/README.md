# User client (A-S1-02 / A-S1-03 / A-S2-01)

Qt Widgets user-facing client baseline. It defaults to deterministic `MockUserService`; `SocketUserService` is available behind an explicit environment switch and follows the v1 contract. Pages are centrally coordinated by `UserWindow` and `SessionManager`.

## Run

The repository uses qmake6 consistently across Qt/C++ modules. The user-client application entry point is `user-client.pro`; the business-adapter and map QtTest entry points are `tests/user-client-tests.pro`, `tests/server-map-service-tests.pro` and the legacy `tests/map-service-tests.pro`. CMake files are not part of this module.

Build the application in a clean directory:

```bash
repo_root="$(pwd)"
mkdir -p ../build/qmake6/user-client
cd ../build/qmake6/user-client
qmake6 "$repo_root/apps/user-client/user-client.pro"
make -j"$(nproc)"
./ev-user-client
```

Build and run QtTest separately:

```bash
repo_root="$(pwd)"
mkdir -p ../build/qmake6/user-client-tests
cd ../build/qmake6/user-client-tests
qmake6 "$repo_root/apps/user-client/tests/user-client-tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./ev-user-client-tests -txt
```

Run the server-map Protocol v1 adapter test with a local fake Socket server:

```bash
mkdir -p ../build/qmake6/server-map-service-tests
cd ../build/qmake6/server-map-service-tests
qmake6 "$repo_root/apps/user-client/tests/server-map-service-tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./ev-server-map-service-tests -txt
```

The client starts in deterministic Mock mode. Set `EV_USER_CLIENT_TRANSPORT=socket` to select `SocketUserService` and the server-owned `ServerMapService`; both use Protocol v1 over `EV_SERVER_HOST`/`EV_SERVER_PORT` (defaults `127.0.0.1:45454`). Against the B PR #4 Schema v0.3 contract now present on `main`, login, profile update, wallet recharge, station/pile queries, active/history orders, reservation transitions, and both reservation and direct charging start (`order_id` or `pile_id`) plus stop/settlement are available. Frozen accounts return status=frozen; the adapter maps 1101 ACCOUNT_FROZEN, 1202 INSUFFICIENT_BALANCE, timeout and connection failures to user-readable messages. Login is phone-only in both Mock and Socket modes and accepts exactly 11 ASCII digits; there is no standalone registration UI or service operation. A legal phone number logs in directly, and the first login can auto-create the account according to the active service contract.

In Socket mode, network work runs outside the GUI thread and completion is returned to widgets through `QFutureWatcher`; login and subsequent station/order queries therefore do not block the window event loop. State-changing operations retain their generated request ID after a connection failure, timeout, protocol failure, or server error, and a retry of the same operation/payload reuses that ID. The ID is retired only after a successful response. A created `pending_reservation` remains visible in the charging page and can be confirmed again or cancelled, including after a lost confirmation response.

Demo phone is `13800000000`; any legal 11-digit ASCII phone can log in directly. The login page has no password or registration fields. `timeout`, `error`, and `server-error` inputs expose failure states without leaking internals.

The home page uses separated dark station cards with light-blue rounded borders and white text, and displays idle-pile count over total-pile count; price is shown only in the station/pile detail and order confirmation. The top-right shortcut buttons were removed in favor of the bottom navigation. The charging page is the single current-status entry and includes a deterministic completed-charge history plus a summary; opening it from the bottom bar does not show the unfinished-order dialog. That dialog appears only when selecting another pile while an order is charging or awaiting settlement. Cancelling an unconfirmed pile selection returns to the pile detail page. Station and pile cards share the same dark/light-blue/white visual treatment.

After selecting an idle pile, the confirmation row provides both `确认创建订单` and `预约该充电桩`; both use the adapter's reservation creation path and are guarded against duplicate submission. The `返回充电桩` action is below settlement. A reserved order is visible in the charging page, where it can be started or ended with `取消预约`; cancellation returns the pile to idle and does not enter completed-charge history. When selecting a non-idle pile, the unavailable message is shown before any active-order message. When selecting an idle pile, an existing reservation reports `已有预约`, while charging or pending settlement reports `未完成订单`. History is returned newest-first and each row displays completion time, charging-station address and amount spent, without an order identifier.

## Map and navigation (A-S2-01)

The map boundary is implemented by `IMapService`, `ServerMapService`, `MockMapService` and `ResilientMapService`. The user client no longer calls Tencent directly and never receives a Tencent credential. `ServerMapService` sends Protocol v1 `map.station.search` and `map.route.plan` requests to B's server; the server owns Tencent WebService calls, cache/audit records, POI persistence and pile snapshot aggregation. The adapter validates the server envelope, station coordinates, route distance/time/polyline and `data_source`/`warning` fields. A new request cancels earlier work, and callbacks are accepted only while the map-request generation and login-session generation still match.

`EV_USER_CLIENT_TRANSPORT=socket` selects the real user service, and the map adapter uses the same `EV_SERVER_HOST`/`EV_SERVER_PORT` endpoint. It sends the logged-in numeric `user_id`; route requests additionally send the selected business `station_id` and `mode` (`driving` or `walking`). Address input is represented as the protocol `origin.kind=address` through `map.station.search`; coordinate input is sent as `origin.kind=coordinate`. POI data supplies only position/name/address. Prices, pile counts/statuses and order operations remain server business data and are never inferred from map data. `server_mock`, stale-cache and upstream warning responses are visibly labelled; an unavailable server automatically falls back to deterministic Mock/offline results.

The `TencentMapService` source remains only as legacy isolated test material from the earlier A-S2-01 implementation; it is not selected by `UserWindow` and is not part of the production map path. `QWebEngineView` now renders the local/offline map surface with server-returned markers and route geometry. The client does not load Tencent GL JS or inject a Key. This respects PR #15's server-only credential boundary and keeps the application usable without external network access.

```bash
sudo apt update
sudo apt install -y qt6-webengine-dev libqt6webenginecore6-bin
```

Build and test the map-enabled client:

```bash
repo_root="$(pwd)"
mkdir -p ../build/qmake6/user-client-map
cd ../build/qmake6/user-client-map
qmake6 "$repo_root/apps/user-client/user-client.pro"
make -j"$(nproc)"

mkdir -p ../user-client-tests ../map-service-tests
cd ../user-client-tests
qmake6 "$repo_root/apps/user-client/tests/user-client-tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./ev-user-client-tests -txt

cd ../map-service-tests
qmake6 "$repo_root/apps/user-client/tests/map-service-tests.pro"
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen QTWEBENGINE_CHROMIUM_FLAGS='--disable-gpu' \
  ./ev-map-service-tests -txt
```

For server-side map acceptance, configure `TENCENT_MAP_KEY` only in B's server environment and run the server's redacted probe. The user client only needs `EV_USER_CLIENT_TRANSPORT=socket`, `EV_SERVER_HOST` and `EV_SERVER_PORT`; no Tencent key is required locally. To validate the client map path, use the server protocol fake or a running B server:

```bash
EV_USER_CLIENT_TRANSPORT=socket ./ev-user-client
```

The original teacher task labels basic real navigation as S1 and optimization/compatibility as A-S2-01. Under the revised PR #15 contract, A-S2-01 on the client means server-protocol adaptation and Mock/offline compatibility; Tencent credential management and upstream calls belong to B.

## Adapter boundary

`IUserService` is the replacement point for B's eventual Socket client. `SocketUserService` is the protocol-v1 implementation and reuses `libs/protocol/protocol.pri`; it sends protocol integer IDs, accepts the canonical B station/pile/order fields, and propagates numeric error codes. In the merged PR #4 contract, `station.list` supplies `pile_total/pile_idle/pile_reserved/pile_charging` but no price or distance; `pile.list` supplies `unit_price_cents_per_kwh`, which is stored on each `Pile`. When the server does not provide a distance, the UI shows `距离待定位` instead of inventing a value. DTOs, pile/order status mapping, completion-time mapping, success/error responses, timeout/connection errors, dynamic pile availability, reservation cancellation and deterministic sample data live in `src/client_service.*`; page code does not access SQLite or wire fields. `orderHistory()` maps B's `order.history.list` operation; profile/recharge map B's persisted integer-cent responses. All protocol field/status changes remain isolated in `SocketUserService`.

## A-S1-02 / A-S2-01 checklist

- [x] 01 baseline/window/navigation/session
- [x] 02 phone-only login validation and Mock failures
- [x] 03 station, detail and pile status/empty/error states
- [x] 04 Tencent Maps URL path and labeled offline route fallback
- [x] 05 reserve → start → stop → settle Mock order flow with duplicate guards
- [x] 06 service adapter and deterministic Mock data
- [x] 07 loading/error/empty/unauthorized feedback
- [x] 08 qmake6 build, QtTest and user-client startup evidence
- [x] A-S2-01 server map protocol adapter, server-owned Tencent boundary, QWebEngineView offline rendering and Mock fallback

Dependency flow: A-S1-01 → A-S1-02-01 → (02,03) → (04,05) → 08; 06 feeds 02–05; 07 feeds 08. B-S1-01/B-S1-02 provide the future real data/Socket replacement contracts; C-S1-03 provides clean-environment build evidence.

## Async request and permission safety

Socket callbacks are accepted only while their captured authentication generation and user ID remain current. The generation changes on login identity changes and logout, not on profile, avatar or wallet refreshes. Station searches discard older request generations; pile results must also match the selected station. A discard cleanup callback restores transient controls such as recharge after logout invalidates a request.

Frozen users retain read and cleanup actions (cancel reservation, stop, settle), while reservation creation/confirmation, charging start/direct start and recharge controls are disabled to match the server contract.
