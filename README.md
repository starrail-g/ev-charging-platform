# EV Charging Platform

东软电动汽车充电桩应用管理平台。

## Modules

- `apps/user-client` — Qt 充电用户端
- `apps/admin-client` — Qt PC 管理端
- `server` — 网络服务与业务服务端
- `libs/common` — Qt/C++ 公共代码
- `libs/protocol` — Socket 通信协议
- `libs/database` — 数据库访问层
- `database` — SQLite Schema / Migration / Seed
- `dashboard` — ECharts 数据可视化大屏
- `ml` — 机器学习与负荷预测
- `docs` — 需求、架构、协议和设计文档
- `tests` — 跨模块集成测试

## Environment

- Ubuntu 22.04+
- Qt Creator 6.2+
- Qt / C++
- SQLite
- Socket
- Multithreading

## Development

详细开发约定参见 `CONTRIBUTING.md` 和 `AGENTS.md`。

当前项目状态参见 `current.md`。

## Workspace Boundary

本目录只存放准备同步或已经同步到 GitHub 的项目内容，包括源码、测试、
正式需求/架构/API/发布文档，以及项目状态文档 `current.md`。

本地过程资料不放入本仓库（与仓库同级存放，下称 `superpowers/`、`build/`）：

- brainstorming、设计规格、实施计划和每日计划：仓库同级 `superpowers/` 目录
- 个人构思与课程/需求原件：仓库同级外层目录
- 本地构建产物：仓库同级 `build/` 目录

正式缺陷日志和发布清单属于 GitHub 项目文档，存放在 `docs/release`。
