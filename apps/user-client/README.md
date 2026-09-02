# User client (A-S1-02)

Qt Widgets user-facing client baseline. It deliberately uses `MockUserService` until B-S1-02 Socket and B-S1-01 data contracts are frozen. Pages are centrally coordinated by `UserWindow` and `SessionManager`.

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

The client starts in deterministic Mock mode. Demo credentials are `13800000000 / 123456`. Registration is two-step: valid phone, password and matching confirmation first; nickname is entered on the second step. Password fields have show/hide eye buttons. `timeout`, `error`, and `server-error` inputs expose failure states without leaking internals.

The home page uses separated dark station cards with light-blue rounded borders and white text, and displays idle-pile count over total-pile count; price is shown only in the station/pile detail and order confirmation. The top-right shortcut buttons were removed in favor of the bottom navigation. The charging page is the single current-status entry and includes a deterministic completed-charge history plus a summary; opening it from the bottom bar does not show the unfinished-order dialog. That dialog appears only when selecting another pile while an order is charging or awaiting settlement. Cancelling an unconfirmed pile selection returns to the pile detail page. Station and pile cards share the same dark/light-blue/white visual treatment.

After selecting an idle pile, the confirmation row provides both `确认创建订单` and `预约该充电桩`; both use the adapter's reservation creation path and are guarded against duplicate submission. The `返回充电桩` action is below settlement. A reserved order is visible in the charging page, where it can be started or ended with `取消预约`; cancellation returns the pile to idle and does not enter completed-charge history. When selecting a non-idle pile, the unavailable message is shown before any active-order message. When selecting an idle pile, an existing reservation reports `已有预约`, while charging or pending settlement reports `未完成订单`. History is returned newest-first and each row displays completion time, charging-station address and amount spent, without an order identifier.

## Map and navigation

`TENCENT_MAP_KEY` is read only from local environment/configuration. The map page always has an offline route path; it labels that result as Mock. The real Tencent Maps API is intentionally not opened in this stage; the disabled button records the future integration point. The map page provides deterministic Mock markers and offline routing. Copy `config/example.env` to an ignored local `.env` only when real integration resumes.

## Adapter boundary

`IUserService` is the replacement point for B's eventual Socket client. DTOs, pile/order status mapping, completion-time mapping, success/error responses, timeout/connection errors, dynamic pile availability, reservation cancellation and deterministic sample data live in `src/client_service.*`; page code does not access SQLite or wire fields. `orderHistory()` and `cancelReservation()` are temporary contracts and should be mapped to B's final endpoints later.

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
