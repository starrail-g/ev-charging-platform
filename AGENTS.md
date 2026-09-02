# Project Instructions --- EV Charging Platform

## Project Scope

This repository implements the EV charging application management
platform defined by the project requirements.

Required product areas include:

-   a Qt user client for station discovery, navigation, user account
    functions, reservation, charging, billing, and settlement
-   a Linux + Qt PC management application for administrator login,
    statistics display, charging-pile management, station management,
    and user management
-   persistent data for users, charging stations, charging piles,
    charging orders, and administrators
-   a Web big-data dashboard using ECharts
-   an intelligent analysis subsystem for charging-load forecasting,
    station recommendation, and load warning
-   Linux application development using Qt/C++, SQLite, Socket
    communication, and multithreading

Treat the project requirements document and explicit team decisions as
the product-level source of truth.

Do not invent required behavior, fields, interfaces, or acceptance
criteria when the requirements are ambiguous. Record unresolved design
questions instead of silently turning assumptions into requirements.

## Repository Map

``` text
apps/
  user-client/       Qt user-facing client
  admin-client/      Qt PC management application

libs/
  common/            reusable C++ utilities and shared types
  protocol/          shared Socket protocol definitions and serialization
  database/          reusable database access code

server/              server-side networking and business logic

database/
  schema/            database schema
  migrations/        schema migrations
  seeds/             development/demo seed data

dashboard/           Web big-data dashboard
ml/                  intelligent analysis / load forecasting subsystem

docs/
  requirements/      requirement notes and traceability
  architecture/      architecture, database, and protocol design
  api/               API documentation
  ui/                UI notes
  meetings/          project meeting notes when needed

scripts/             setup/build/test helpers
config/              example project configuration
tests/integration/   cross-module integration tests
```

Do not create parallel implementations of the same responsibility in
unrelated directories.

If the repository structure changes intentionally, keep this map
consistent with the actual repository.

## Architecture Boundaries

Keep presentation, networking, business logic, and persistence
separated.

### User Client

`apps/user-client` owns user-facing Qt UI and client-side interaction.

Its scope includes the required user functions such as:

-   nearby charging-station presentation
-   simulated location/address selection
-   Tencent Maps integration
-   navigation display
-   phone-number login/registration UI
-   profile and wallet UI
-   reservation, charging, billing, and settlement UI

Do not place server-side persistence or administrator functionality in
this module.

### Admin Client

`apps/admin-client` owns the Linux + Qt management UI.

Its scope includes:

-   administrator login
-   revenue/statistics visualization
-   charging-pile status and management
-   station management
-   user management

Keep reusable server, protocol, and persistence logic outside Qt widget
classes.

### Server

`server` is the intended integration point for Socket communication and
shared business logic.

Current target data flow:

``` text
Qt clients
    |
    | Socket / defined protocol
    v
server
    |
    v
database layer
    |
    v
SQLite
```

Do not make networked clients depend directly on the runtime SQLite
database unless the team explicitly changes this architecture.

### Protocol

`libs/protocol` owns shared communication contracts.

Do not define incompatible ad-hoc message formats independently in
different modules.

A protocol change is cross-module work. When changing a message
contract:

1.  inspect all producers and consumers
2.  update the protocol documentation
3.  update affected client/server code
4.  validate the changed communication path

### Database

The required persistent entities include at least:

-   users
-   charging stations
-   charging piles
-   charging orders
-   administrators

The exact schema is not fixed until it is explicitly designed and
documented.

Keep SQL and persistence concerns out of Qt widget classes.

Schema changes must account for all dependent modules and should update
the database design documentation together with the implementation.

### Dashboard

`dashboard` owns the Web big-data visualization.

Use ECharts for the required dashboard visualization.

Do not assume a specific frontend framework until the project explicitly
adopts one.

## UI, Theme, and Visual Design

Treat user experience and visual quality as first-class product concerns
across the Qt user client, Qt admin client, Web dashboard, and analytical
outputs. After functional correctness, security, data integrity, and
reliability are protected, the interface should be a distinguishing
strength of the project rather than a cosmetic afterthought.

When work materially affects layout, navigation, interaction, theming,
data visualization, or the overall visual language:

-   make the design choices an explicit part of the discussion before
    implementation; compare meaningful alternatives when the direction
    is not obvious
-   search the Web for relevant, current examples, mature products,
    design-system guidance, and lessons from comparable energy,
    mobility, operations, and data-platform projects
-   summarize the useful references and explain which ideas are being
    adopted or rejected; do not copy another product's identity or use
    unlicensed assets
-   establish a coherent visual direction for color, typography,
    spacing, shape, iconography, motion, hierarchy, and component states
    before polishing individual screens
-   reuse named design tokens and shared components so light/dark themes,
    Qt screens, dashboards, charts, and exported analytical views feel
    intentionally related
-   design complete interaction states, including loading, empty,
    success, warning, error, disabled, offline, and permission-denied
    states where applicable
-   keep visualizations truthful and legible: choose chart forms based on
    the analytical question, preserve scale/context, label units and time
    ranges, and do not trade comprehension for decoration
-   validate important interfaces at representative window sizes and
    with realistic Chinese content and data; check contrast, keyboard
    navigation, focus visibility, readability, and color-independent
    status cues

Aim for a visual identity that is polished, contemporary, distinctive,
and appropriate to an EV charging platform. Novelty must serve clarity
and the product story. Visual refinement must never conceal system state,
misrepresent data, weaken accessibility, or compromise secure and correct
behavior.

### Intelligent Analysis

`ml` owns the intelligent analysis subsystem.

Required analysis capabilities include:

-   charging-load forecasting for future 1-hour, 6-hour, and 24-hour
    periods
-   use of relevant historical charging data and factors such as weather
    and holidays
-   low-congestion station recommendation
-   load warning

The project requirements do not fix a machine-learning framework or
implementation language. Do not introduce one as a project-wide
requirement without an explicit project decision.

## Qt / C++ Development

Target the project environment:

-   Ubuntu 22.04 or later
-   Qt Creator 6.2 or later
-   Qt / C++
-   SQLite
-   Socket communication
-   multithreading where appropriate

Follow the Qt version and APIs actually available in the development
environment.

Keep Qt widget classes focused on presentation and interaction. Put
reusable business logic in appropriate non-UI classes/modules.

Do not block the GUI thread with long-running network, database,
forecasting, or other expensive work.

For multithreaded code:

-   define object ownership and lifetime clearly
-   avoid unnecessary shared mutable state
-   use Qt signals/slots or another established project mechanism for
    cross-thread communication
-   never update GUI widgets directly from worker threads
-   ensure shutdown does not leave background work accessing destroyed
    objects

## Charging and Data Consistency

Charging, billing, settlement, wallet balance, charging-pile state, and
order state are consistency-sensitive.

When implementing these flows:

-   define state transitions explicitly
-   validate invalid or duplicate operations
-   handle database failures deliberately
-   avoid partial updates that leave related state inconsistent
-   make error paths observable instead of silently ignoring them

Correct state is more important than UI convenience.

## External Integration

The user client requires Tencent Maps Web API related functionality.

Keep the external-service boundary explicit so development can use
controlled configuration or mock/simulated behavior when necessary.

Do not hard-code project credentials into integration code.

## Documentation Coupling

When a cross-module design changes, update the corresponding project
documentation:

``` text
Database design    -> docs/architecture/database.md
Socket protocol    -> docs/architecture/protocol.md
System architecture -> docs/architecture/README.md
API contracts      -> docs/api/
```

Do not create documentation merely to duplicate implementation details.

## Project State Tracking

Every change to project code or architecture must also maintain
`current.md`:

-   reflect the new implementation/architecture state in `current.md`
    (status, decisions, recent history) as part of the change
-   compress `current.md` as it grows so it keeps only currently
    valuable information; condense or drop stale entries instead of
    appending indefinitely

## Workspace Boundary

This Git repository contains only source code and formal project
artifacts intended for GitHub. Keep `current.md` here as the tracked
project-state document.

Store local process material outside the repository, typically in
sibling directories (e.g. `superpowers/` and `build/` next to this repo):

-   brainstorming, design specs, implementation plans, and daily plans
    belong under a sibling `superpowers` folder
-   personal ideas and source/course documents belong directly under
    another clearly named outer folder
-   generated build output belongs under a sibling `build` folder

Do not create `IDEA.md`, `docs/superpowers`, or physical build output
inside this repository. Formal requirements, architecture, API, test,
defect, and release documents remain in their normal repository
directories because they are GitHub project artifacts.

## Build and Validation

The project requirements do not mandate CMake or qmake.

Do not treat either as authoritative until the repository explicitly
adopts a build system.

For implementation work, validate the affected module using the
build/test path that actually exists in the repository.

Protocol and database changes require validation of their affected
integration paths.

If a required validation path does not yet exist, report that limitation
rather than treating the work as verified.
