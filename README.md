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
