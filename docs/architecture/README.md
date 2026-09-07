# 架构说明

本目录记录系统级架构设计。详细模块文档见各自文件。

## 系统组成

``` text
Qt 用户端 (apps/user-client)        Qt 管理端 (apps/admin-client)
        │                                    │
        └────────── Socket ──────────────────┤
                                             ▼
                     server（Socket/业务层）
                                             │
                                             ▼
                     database layer → SQLite

Web 大屏 (dashboard) —— 读取可追溯统计数据/数据集（与 server 数据同口径）
ml（智能分析，扩展项）
```

## 文档索引

| 文档 | 内容 |
|---|---|
| [README.md](README.md) | 本页：系统组成与文档索引 |
| [admin-client.md](admin-client.md) | 管理端：页面层、Repository 抽象、Mock/Socket 双实现、数据方向 |
| [database.md](database.md) | 数据库设计、v0.3 迁移和事务边界（B 负责） |
| [protocol.md](protocol.md) | Socket 协议 v1、错误码和用户生命周期接口（B 负责） |
| [map-service-protocol.md](map-service-protocol.md) | 服务端腾讯地图、站点同步、路线查询和模拟桩状态的 Protocol v1 扩展 |

## 管理端/大屏在系统中的位置

- **管理端**：PC 端管理界面，经 `AdminRepository` 抽象访问数据；第一阶段默认 Mock，9/7 18:00 闸门后接入 Socket。边界见 [admin-client.md](admin-client.md)。
- **大屏**：Web 可视化，本地 HTTP 静态服务加载固定 JSON 演示数据；不直接访问 SQLite，数据口径与管理端 Mock 保持一致。说明见 `dashboard/README.md`。
- **地图服务**：目标架构由服务端统一持有腾讯地图 Key、调用 WebService、缓存并记录脱敏结果；用户端只经 Socket 获取站点和路线数据并本地绘制。契约已冻结，服务端实现和 Schema v0.4 迁移尚未完成。
