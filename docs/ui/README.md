# UI 说明

本目录记录用户端、管理端和大屏的 UI 说明与截图索引。

## 用户端文档

- [用户端详细要求与腾讯地图接入记录](user-client-detailed-requirements.md)：功能基线、页面验收、Mock/真实接口边界、API 探测方案和相似 GitHub 项目调研。
- [用户端实现说明](user-client.md)：当前 Qt Widgets/Mock 实现和后续适配位置。

## 用户端边界

- 用户端路径为 `apps/user-client`，通过 `IUserService` 和 Socket Protocol v1 访问服务端，不直接操作 SQLite。
- Mock/离线路径用于并行开发和无网络演示，不代表真实后端业务已完成。
- S1 真实导航属于主链路；腾讯地图失败、无 Key 或无网络时必须保留明确的 Mock/离线回退。
- 智能分析结果只展示 B/模型服务提供的预测、推荐和预警，并标注数据时间与降级状态。

## 管理端页面清单

| 页面 | 文件 | 状态 | 截图 |
|---|---|---|---|
| 登录页 | `apps/admin-client/src/pages/loginpage.*` | 已有 Mock/TDD，真实接口待联调 | — |
| 概览页 | `apps/admin-client/src/pages/overviewpage.*` | 已有 Mock 四态，真实统计待联调 | — |
| 桩管理页 | `apps/admin-client/src/pages/pilepage.*` | 页面/接口待完成 | — |
| 站点管理页 | `apps/admin-client/src/pages/stationpage.*` | 页面/接口待完成 | — |
| 用户管理页 | `apps/admin-client/src/pages/userpage.*` | 页面/接口待完成 | — |

## 大屏

| 入口 | 用途 | 截图 |
|---|---|---|
| `dashboard/index.html` | 标准入口（本地 HTTP） | — |
| `dashboard/offline.html` | 离线备用入口（无 fetch/CDN） | — |

## 截图索引规则

- 截图统一存放：`docs/ui/screenshots/`（提交前确认大小与必要性）。
- 命名：`<页面/入口>-<状态>-<日期>.png`，如 `overview-normal-20260901.png`。
- 答辩证据截图必须与录屏和对应提交版本处于同一 commit。
- UI 文档不得重新定义 Socket 字段、数据库状态或错误码；以协议、数据库文档和 `current.md` 为准。