# Current Project State

## Overview

Project: EV Charging Platform / 电动汽车充电桩应用管理平台

The project implements the application management platform defined by
the project requirements.

Required product areas include:

-   Qt user client
-   Linux + Qt PC management application
-   server-side communication/business processing
-   SQLite-backed persistent data
-   ECharts Web big-data dashboard
-   intelligent charging-load analysis and forecasting

Current stage: **stage-I backend contract and diagnostic server
foundation**.

No production feature should currently be treated as complete unless
verified in the repository.

## Status

Completed project setup work:

-   project requirements have been obtained and reviewed
-   monorepo skeleton has been created
-   basic Git/GitHub collaboration guidance has been prepared
-   project-level Codex instructions are defined in `AGENTS.md`
-   persistent project state is tracked in this file

Current implementation state:

-   SQLite schema v0.1, deterministic seed data, and transaction rules are
    documented and validated with Python's SQLite driver
-   Socket protocol v1 framing/envelope/error codes are frozen in
    `docs/architecture/protocol.md`
-   a Qt TCP server skeleton supports `health` and `echo`
-   database-backed business handlers and Qt clients are not yet implemented
-   module APIs are not yet finalized
-   qmake6 project files exist for the protocol tests and server; project-wide
    build-system adoption is still pending
-   automated CI is not currently a project priority

## Architecture

Current target module layout:

``` text
apps/user-client       Qt user client
apps/admin-client      Qt PC management application

server                 Socket/server business layer

libs/common            shared C++ utilities/types
libs/protocol          shared communication protocol
libs/database          database access layer

database               schema, migrations, seed data

dashboard              ECharts Web dashboard

ml                     intelligent analysis subsystem

docs                    project design/documentation
tests/integration       cross-module integration tests
```

Current target data flow for networked application features:

``` text
User/Admin Qt Client
        |
        | Socket
        v
      Server
        |
        v
 Database Layer
        |
        v
      SQLite
```

This separation is a project architecture decision for the current
implementation plan, not a statement that the requirements document
fixes the exact process layout.

Important architectural details are still pending design.

## TODO

High priority:

-   [x] confirm team member responsibilities and first development tasks
-   [ ] decide and document the concrete system architecture
-   [x] design initial SQLite schema
-   [x] design Socket message framing and core protocol
-   [x] initialize the server buildable project and diagnostic path
-   [ ] choose the Qt/C++ build system used across all modules
-   [ ] initialize the user-client buildable Qt project
-   [ ] initialize the admin-client buildable Qt project
-   [ ] define the first end-to-end vertical feature

After foundations are stable:

-   [ ] implement required user-client features
-   [ ] implement required admin-management features
-   [ ] establish dashboard data path and ECharts pages
-   [ ] define intelligent-analysis data pipeline
-   [ ] implement 1h / 6h / 24h charging-load forecasting
-   [ ] implement station recommendation and load warning
-   [ ] add integration and regression tests for stable flows

## Known Issues

-   database-backed service handlers are not implemented yet
-   authentication/session behavior beyond the protocol v1 payload contract
    needs design before mutating handlers are exposed
-   charging-order state machine and settlement consistency rules are
    documented in the v0.1 database/protocol contracts; handlers remain
    unimplemented
-   dashboard-to-backend data interface is undecided
-   ML framework/language and model approach are undecided
-   external Tencent Maps API integration details and development-key
    handling need design
-   CMake versus qmake has not been decided for the whole repository; current
    protocol/server validation uses `qmake6`

These are unresolved design items, not implementation defects.

## Decisions

-   use a single monorepo for the complete project
-   organize source code by module/responsibility rather than by
    contributor
-   keep Qt presentation code separate from reusable
    protocol/database/business logic
-   use `server` as the intended central Socket/business integration
    layer
-   keep shared communication contracts under `libs/protocol`
-   keep persistent-data access separated from UI code
-   use `current.md` as concise persistent project state for Codex
    sessions
-   keep global Codex delegation/runtime rules in the user's global
    Codex configuration and global `AGENTS.md`; keep this repository's
    `AGENTS.md` project-specific
-   do not prematurely lock in details that the requirements do not
    specify
-   同学 B 负责 `server`、`libs/protocol`、`libs/database` 和
    `database`；其阶段计划与系统整体认知记录在
    `docs/development-plan.md`

## Recent History

-   reviewed the project requirements and identified the main product
    modules
-   created the initial monorepo directory skeleton
-   prepared a beginner-friendly Git/GitHub collaboration guide
-   established the project-specific `AGENTS.md`
-   initialized `current.md`
-   reviewed the original project brief and documented the team-B
    development plan and subsystem dependencies
-   added a backend requirement traceability baseline for database and
    protocol implementation
-   implemented and validated SQLite v0.1, protocol v1, and a diagnostic
    Qt TCP server skeleton

Keep this history concise. Compress or replace old entries as the
project progresses rather than appending indefinitely.
