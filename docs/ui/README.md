# UI 说明

本目录记录 UI 相关说明与截图索引。

## 管理端页面清单

| 页面 | 文件 | 状态 | 截图 |
|---|---|---|---|
| 登录页 | `apps/admin-client/src/pages/loginpage.*` | 待实现（9/2） | — |
| 概览页 | `apps/admin-client/src/pages/overviewpage.*` | 待实现（9/3） | — |
| 桩管理页 | `apps/admin-client/src/pages/pilepage.*` | 待实现（9/4-5） | — |
| 站点管理页 | `apps/admin-client/src/pages/stationpage.*` | 待实现（9/4-5） | — |
| 用户管理页 | `apps/admin-client/src/pages/userpage.*` | 待实现（9/4-5） | — |

## 大屏

| 入口 | 用途 | 截图 |
|---|---|---|
| `dashboard/index.html` | 标准入口（本地 HTTP） | — |
| `dashboard/offline.html` | 离线备用入口（无 fetch/CDN） | — |

## 截图索引规则

- 截图统一存放：`docs/ui/screenshots/`（git 提交时确认大小与必要性）。
- 命名：`<页面/入口>-<状态>-<日期>.png`，如 `overview-normal-20260901.png`。
- 用于答辩证据的截图必须与录屏/提交版本同 commit。
