# User client (A-S1-02 / A-S1-03)

Qt Widgets user-facing client baseline. It defaults to deterministic `MockUserService`; `SocketUserService` is available behind an explicit environment switch and follows the v1 contract. Pages are centrally coordinated by `UserWindow` and `SessionManager`.

## Run

The repository uses qmake6 consistently across Qt/C++ modules. The user-client application entry point is `user-client.pro`; the QtTest entry point is `tests/user-client-tests.pro`. CMake files are not part of this module.

Build the application in a clean directory:

```bash
mkdir -p build/qmake6/user-client
cd build/qmake6/user-client
qmake6 ../../../apps/user-client/user-client.pro
make -j"$(nproc)"
./ev-user-client
```

Build and run QtTest separately:

```bash
mkdir -p build/qmake6/user-client-tests
cd build/qmake6/user-client-tests
qmake6 ../../../apps/user-client/tests/user-client-tests.pro
make -j"$(nproc)"
QT_QPA_PLATFORM=offscreen ./ev-user-client-tests -txt
```

The client starts in deterministic Mock mode. Set `EV_USER_CLIENT_TRANSPORT=socket` to select `SocketUserService`; the adapter sends protocol v1 envelopes with UUID request IDs to `EV_SERVER_HOST`/`EV_SERVER_PORT` (defaults `127.0.0.1:45454`). Against B PR #4 latest head 20a3815 (PR #4 merged main; Schema v0.3), login, profile update, wallet recharge, station/pile queries, active/history orders, reservation transitions, and charging start/stop/settlement are available. Frozen accounts return status=frozen; the adapter maps 1101 ACCOUNT_FROZEN, 1202 INSUFFICIENT_BALANCE, timeout and connection failures to user-readable messages. Login is phone-only per protocol v1; the registration page remains a Mock-only convenience flow until a server registration operation is defined.

Demo credentials are `13800000000`. Registration is two-step: valid phone, password and matching confirmation first; nickname is entered on the second step. Password fields have show/hide eye buttons. `timeout`, `error`, and `server-error` inputs expose failure states without leaking internals.

The home page uses separated dark station cards with light-blue rounded borders and white text, and displays idle-pile count over total-pile count; price is shown only in the station/pile detail and order confirmation. The top-right shortcut buttons were removed in favor of the bottom navigation. The charging page is the single current-status entry and includes a deterministic completed-charge history plus a summary; opening it from the bottom bar does not show the unfinished-order dialog. That dialog appears only when selecting another pile while an order is charging or awaiting settlement. Cancelling an unconfirmed pile selection returns to the pile detail page. Station and pile cards share the same dark/light-blue/white visual treatment.

After selecting an idle pile, the confirmation row provides both `确认创建订单` and `预约该充电桩`; both use the adapter's reservation creation path and are guarded against duplicate submission. The `返回充电桩` action is below settlement. A reserved order is visible in the charging page, where it can be started or ended with `取消预约`; cancellation returns the pile to idle and does not enter completed-charge history. When selecting a non-idle pile, the unavailable message is shown before any active-order message. When selecting an idle pile, an existing reservation reports `已有预约`, while charging or pending settlement reports `未完成订单`. History is returned newest-first and each row displays completion time, charging-station address and amount spent, without an order identifier.

## Map and navigation

`TENCENT_MAP_KEY` is read only from local environment/configuration. The map page always has an offline route path; it labels that result as Mock. The real Tencent Maps API is intentionally not opened in this stage; the disabled button records the future integration point. The map page provides deterministic Mock markers and offline routing. Copy `config/example.env` to an ignored local `.env` only when real integration resumes.

## Adapter boundary

`IUserService` is the replacement point for B's eventual Socket client. `SocketUserService` is the protocol-v1 implementation and reuses `libs/protocol/protocol.pri`; it sends protocol integer IDs, accepts the canonical B station/pile/order fields, and propagates numeric error codes. In the merged PR #4 contract, `station.list` supplies `pile_total/pile_idle/pile_reserved/pile_charging` but no price or distance; `pile.list` supplies `unit_price_cents_per_kwh`, which is stored on each `Pile`. When the server does not provide a distance, the UI shows `距离待定位` instead of inventing a value. DTOs, pile/order status mapping, completion-time mapping, success/error responses, timeout/connection errors, dynamic pile availability, reservation cancellation and deterministic sample data live in `src/client_service.*`; page code does not access SQLite or wire fields. `orderHistory()` maps B's `order.history.list` operation; profile/recharge map B's persisted integer-cent responses. All protocol field/status changes remain isolated in `SocketUserService`.

## A-S1-02 checklist

- [x] 01 baseline/window/navigation/session
- [x] 02 login/register/validation and Mock failures
- [x] 03 station, detail and pile status/empty/error states
- [x] 04 Tencent Maps URL path and labeled offline route fallback
- [x] 05 reserve → start → stop → settle Mock order flow with duplicate guards
- [x] 06 service adapter and deterministic Mock data
- [x] 07 loading/error/empty/unauthorized feedback
- [x] 08 qmake6 build, QtTest and user-client startup evidence

Dependency flow: A-S1-01 → A-S1-02-01 → (02,03) → (04,05) → 08; 06 feeds 02–05; 07 feeds 08. B-S1-01/B-S1-02 provide the future real data/Socket replacement contracts; C-S1-03 provides clean-environment build evidence.
