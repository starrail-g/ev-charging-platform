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

## Quick Start（第一阶段演示）

- **Web 大屏**：`python dashboard/serve.py --port 61469`（Ubuntu：`python3`），浏览器开
  `http://127.0.0.1:61469/`；答辩离线演练加 `?map=topology`。详见 `dashboard/README.md`。
- **Qt 管理端**：`apps/admin-client`（qmake）；登录 `admin/123456`；闸门联调时
  `EV_ADMIN_DATA_SOURCE=socket` 切换真实 Socket（默认 Mock 演示）。详见
  `docs/ui/README.md` 与 `tests/integration/role-c-smoke-test.md`。
- **服务端/数据库**：启动与迁移命令见 `server/README.md`、`database/README.md`；
  v0.3 库迁移用 `scripts/migrate_db.py`（迁移失败自动回滚，原子）。
- **密钥**：复制 `config/example.env` 为本地 `config/local.env` 再填写；真实密钥永不入库。
- 阶段一交付清单与核对：`docs/release/stage1-checklist.md`；每日状态：`current.md`。

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
