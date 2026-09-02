# Current Project State

## Overview

Project: EV Charging Platform / 电动汽车充电桩应用管理平台

The project implements the application management platform defined by the
project requirements, including Qt user/admin clients, a Socket server,
SQLite persistence, an ECharts dashboard, and intelligent analysis.

Current stage: **stage-I integration: backend user lifecycle + admin-client
skeleton**.

No production feature should be treated as complete unless verified in the
repository.

## Status

-   The repository keeps source and formal project artifacts; local plans and
    generated builds belong in sibling `superpowers/` and `build/` folders.
-   qmake (`.pro`) is the project build system; CMake is not maintained.
-   SQLite schema v0.2, deterministic seed data, atomic migration, and
    settlement consistency rules are documented and tested.
-   The Qt TCP server implements `health`, `echo`, `user.login`, station/pile
    queries, active/history order queries, reservation transitions, and
    charging start/stop/settlement.
-   Lifecycle mutations use request-ID replay records; direct idle-pile start
    and frozen-user guards are implemented.
-   A clean database can load repeatable development seed data through
    `EV_DATABASE_SEED_PATH`; existing databases are not reseeded.
-   The admin client now has a buildable Qt Widgets shell, login flow,
    repository abstraction, Mock data source, overview state handling, and
    Qt Test coverage. It does not yet use a Socket repository.
-   Administrator, recharge, profile, statistics, and management mutation
    handlers remain pending on the server; user-client, dashboard, and ML
    implementations remain in later stages.

## Architecture

``` text
Qt user client / Qt admin client
              |
              | Socket / protocol v1
              v
        server business layer
              |
              v
        database layer -> SQLite

dashboard and ML consume defined, traceable data interfaces separately.
```

Presentation code remains separate from networking, business rules, and
persistence. Admin pages use `AdminRepository`; they do not create sockets or
write SQL directly. The current admin repository is Mock and is designed to be
replaced by a Socket adapter after the interface gate.

## TODO

Completed foundations:

-   [x] design and validate SQLite schema v0.2, seed, migration, and
    transaction constraints
-   [x] freeze protocol v1 framing, envelope, error codes, and user lifecycle
    operations
-   [x] implement and validate stage-I server/database user lifecycle
-   [x] add request-ID idempotency, order history, clean seed startup, and
    migration failure-path tests
-   [x] merge the admin-client qmake skeleton and login/overview tests from
    the updated `main`

Next priorities:

-   [ ] freeze administrator/statistics/pile/station/user API fields with A/C
    and implement the corresponding server handlers
-   [ ] build `SocketAdminRepository` against the frozen admin contract
-   [ ] complete admin pile/station/user pages and ECharts dashboard
-   [ ] initialize the user-client Qt project and its Socket integration
-   [ ] define and implement the intelligent-analysis data pipeline and
    1h/6h/24h forecasting, recommendation, and warning
-   [ ] add cross-module integration and regression tests

## Known Issues

-   Existing in-progress orders are not automatically closed when an
    administrator freezes a user; freeze currently blocks subsequent lifecycle
    mutations.
-   Administrator login, statistics, recharge, profile, and management
    mutation operations are contracted but not implemented by the server.
-   The admin client remains Mock-backed until the Socket adapter and interface
    gate are complete.
-   Authentication/session behavior beyond payload-level ID checks needs a
    later token/session design.
-   Dashboard data ownership, ML framework/language, and Tencent Maps key
    handling remain design items.

## Decisions

-   Use one monorepo organized by module responsibility.
-   Keep `server` as the central Socket/business integration layer and keep
    SQLite behind the database module.
-   Use qmake `.pro` files as the supported build path.
-   Use `current.md` as concise persistent project state and update it with
    meaningful code or architecture changes.
-   Keep process material and build output outside the repository; do not
    commit credentials, tokens, or generated binaries.
-   B owns `server`, `libs/protocol`, `libs/database`, and `database`;
    C owns the admin client/dashboard/test-release material; A owns the user
    client.

## Recent History

-   merged updated `origin/main` admin-client skeleton into the backend feature
    branch and resolved API/state-document conflicts
-   validated the merged backend with database tests, protocol tests, qmake
    server build, seeded smoke flow, and concurrent idempotency replay
-   fixed direct charging start, frozen-user guards, request-ID idempotency,
    order history, seed startup, and migration request-record compatibility
-   implemented SQLite v0.2, protocol v1, and the initial TCP server
-   established the monorepo, team responsibilities, development plan, and
    project collaboration guidance

Keep this history concise; replace stale details instead of appending raw logs.
