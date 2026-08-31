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

Current stage: **repository initialization and architecture
preparation**.

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

-   core business features have not yet been implemented
-   Socket protocol is not yet finalized
-   database schema is not yet finalized
-   module APIs are not yet finalized
-   build system is not yet finalized
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

-   [ ] confirm team member responsibilities and first development tasks
-   [ ] decide and document the concrete system architecture
-   [ ] design initial SQLite schema
-   [ ] design Socket message framing and core protocol
-   [ ] choose the Qt/C++ build system used by the repository
-   [ ] initialize the user-client buildable Qt project
-   [ ] initialize the admin-client buildable Qt project
-   [ ] initialize the server buildable project
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

-   exact Socket protocol and serialization format are undecided
-   exact SQLite tables, fields, constraints, and relationships are
    undecided
-   authentication/session behavior beyond the stated product
    requirements needs design
-   charging-order state machine and settlement consistency rules need
    design
-   dashboard-to-backend data interface is undecided
-   ML framework/language and model approach are undecided
-   external Tencent Maps API integration details and development-key
    handling need design
-   CMake versus qmake has not been decided
-   team ownership of modules has not yet been recorded here

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

## Recent History

-   reviewed the project requirements and identified the main product
    modules
-   created the initial monorepo directory skeleton
-   prepared a beginner-friendly Git/GitHub collaboration guide
-   established the project-specific `AGENTS.md`
-   initialized `current.md`

Keep this history concise. Compress or replace old entries as the
project progresses rather than appending indefinitely.
