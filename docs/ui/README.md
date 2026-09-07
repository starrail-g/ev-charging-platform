# UI 说明（管理端 + 大屏统一视觉与页面映射）

> 统一视觉系统交付说明（Task 12 Step 1 落盘）：令牌源、五态语义色、结构色禁用边界、
> 地图降级、动画规范、双端页面映射。用户端说明与截图索引规则见文末。
> UI 文档不得重新定义 Socket 字段、数据库状态或错误码；以协议、数据库文档和 `current.md` 为准。

## 1. 视觉令牌（唯一源与生成链）

- **唯一手工维护源**：`libs/common/ui/design-tokens.json`（昼夜两套语义色 + shape + motion）。
- **生成**：`python scripts/generate_ui_tokens.py`（含对比度门禁：正文/语义色 ≥4.5:1、
  拓扑信息线 ≥3.0:1）→ 四份生成文件：Qt `apps/admin-client/resources/generated/theme.qss`、
  `src/theme/generated/theme_tokens.h`、Web `dashboard/css/generated/theme.css`、
  `dashboard/js/generated/theme-tokens.js`。
- **纪律**：生成文件随源码提交但**不得手改**——只改 tokens/模板后重跑生成器；
  Qt src.pro 有 pre-link `--check` 钩子防漂移；改 `theme.template.css` 后同样必须重跑。

## 2. 桩五态语义（双端同口径，protocol.md）

| 协议值 | 中文 | 语义色（日班） | 语义色（夜班） | 图形（Qt StatusGlyphWidget） |
|---|---|---|---|---|
| idle | 空闲 | #2A7442 | #B7F36A | 对勾圆 |
| reserved | 已预约 | #8A5A00 | #F5C451 | 时钟虚线 |
| charging | 充电中 | #0E6E8C | #4DD7FF | 闪电点 |
| fault | 故障 | #A94B38 | #FF806D | 警告三角 |
| offline | 离线 | #5B666B | #A6B0B4 | 断链 |
| unknown（解析失败兜底，协议无此值） | 未知 | #4E5A55 | #93A19B | 问号菱形 |

- 拓扑/聚合取"最需关注"状态：fault > offline > charging > reserved > idle（Qt priority 数组与
  Web map-surface 同序）；attention = fault + offline。
- 未知态可展示、**不参与**关注排序/脉冲动画（C-S1-019 语义）。
- 背景/文本/结构色：日班背景 #EDF0EE、文本 #18201D、弱化 #5F7068；夜班 #0A1110/#E9F5ED/#A6B0B4。
- 语义色只用于状态/强调元素；正文、边框、分隔等一律走结构令牌，**禁止直接手写色值**代替令牌。

## 3. 动画与无障碍降级

- 时长令牌（ms）：micro 150 / panel 200 / chargingPulse 2600 / faultPulse 2800 / aurora 11000；
  Qt 用 `kMotion*PulseMs` 常量、Web 用 theme-tokens 的 motion 组。
- 统一动效开关：环境变量 `EV_UI_REDUCED_MOTION=1`（Qt）停全部动效（呼吸/脉冲/极光）；
  Web 尊重 prefers-reduced-motion 且拓扑/状态不依赖动画传达信息。
- 键盘可达：Qt 拓扑图 StrongFocus + ←/→ 切站、Enter/Space 激活，聚焦环用主题 focus 令牌；
  Web 地图站点卡片可 Tab 聚焦。

## 4. 地图降级决策（Web）

- 统一地图表面 `MapSurface`：**真实地图优先（腾讯地图 JS API GL，Key 本地配置）/ 离线 SVG
  拓扑兜底**；`?map=topology` 是答辩离线演练强制开关；无 Key/超时/被阻断自动降级，主屏不白屏。
- 腾讯 Key 只走 `config/local.env` 或环境变量 `TENCENT_MAP_KEY`（优先），永不进仓库/日志/截图。
- 渲染器生命周期归 MapSurface 单点（mount/destroy 幂等 + 代际防竞态）；拓扑与真实地图共用同一
  经纬度数据（demo.json）。

## 5. 双端页面与组件映射

| 业务 | Qt 管理端（apps/admin-client） | Web 大屏（dashboard） |
|---|---|---|
| 登录/会话 | LoginPage（1100 UNAUTHORIZED 分支） | —（大屏为公开展示） |
| 营收 | OverviewPage 概览卡（7d + 30d 副行） | charts.js 营收趋势（7d/30d 切换） |
| 桩五态 | 桩页表格 8 列 + StatusTag | 状态分布图 + 态势图站点着色 |
| 桩级异常 | 需关注列表 + 故障/离线呼吸脉冲 | 告警联动（badge/图表），点击联动 |
| 站点 | 站页 5 列（在线率） | 地图 + 利用率排行 |
| 用户 | 用户页 5 列 + 冻结/解冻（Mock 模拟标注） | — |
| 数据口径 | MockAdminRepository/mockdataset | data-adapter/demo.json（金额整数分、UTC、五态同源） |
| 管理动作 | restartPile / setUserStatus（模拟或 Socket） | —（大屏只读） |

- 数据链路：页面 → `AdminRepository` 抽象（Mock 500ms / Socket 适配层）→ 展示；页面不建 Socket
  不写 SQL；Mock 与 demo.json 数据同口径（改一侧必须同步另一侧 + 双端测试）。

## 6. 管理端页面清单（2026-09-04 状态）

| 页面 | 文件 | 状态 |
|---|---|---|
| 登录页 | `apps/admin-client/src/pages/loginpage.*` | Mock 登录全流程 + 错误分支测试；Socket 待闸门 |
| 概览页 | `apps/admin-client/src/pages/overviewpage.*` | Mock 四态 + 30d 副行；真实统计待闸门 |
| 充电桩页 | `apps/admin-client/src/pages/pilepage.*` | 8 列/筛选/聚焦 + 重启按钮（C-S1-005 Mock 模拟） |
| 充电站页 | `apps/admin-client/src/pages/stationpage.*` | 5 列含在线率 |
| 用户管理页 | `apps/admin-client/src/pages/userpage.*` | 5 列 + 冻结/解冻按钮（C-S1-007 Mock 模拟） |

动作模拟边界：重启/冻结成功文案带"（模拟）"后缀，正常态页脚常驻提示"9/7 接口闸门后接入真实
服务端"；状态栏数据来源标识来自 Repository（Mock 演示 / Socket(host:port)），不硬编码。

## 7. 用户端文档与边界

- [用户端详细要求与腾讯地图接入记录](user-client-detailed-requirements.md)
- [用户端实现说明](user-client.md)
- 用户端路径 `apps/user-client`，经 `IUserService` + Socket Protocol v1 访问服务端；
  Mock/离线路径只用于并行开发与无网络演示，不代表真实后端业务已完成；
  腾讯地图失败/无 Key/无网络必须保留明确 Mock/离线回退；智能分析结果只展示 B/模型服务
  提供的预测并标注数据时间与降级状态。

## 8. 截图索引规则

- 截图统一存放 `docs/ui/screenshots/`；命名 `<页面/入口>-<状态>-<日期>.png`。
- 答辩证据截图必须与录屏和对应提交版本处于同一 commit。
