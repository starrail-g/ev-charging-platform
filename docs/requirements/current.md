# 统一 UI 交付验收记录（Task 12 Step 7）

> 本文件只写**已经实现并通过验证**的能力（不提前宣称预测模型、生产地图配置或
> 真实后端联调完成）；项目整体状态见仓库根 `current.md`（三块纪律：已提交/仅本地/待办）。
> 最后更新：2026-09-04（管理操作批 + Socket 批本地验证后待续）。

## 1. 实际验证平台与命令（9/4 基线）

- Windows 11（开发机）：Qt 6.2.4 mingw + MinGW 13.1，qmake 工程，构建目录仓库外
  `D:/work/chargingplatform/build/admin-client-ui`；`QT_QPA_PLATFORM=offscreen` 直跑三套 exe。
- Ubuntu VM（BitDev，root/123456，vmrun 直登）：qmake6 + offscreen（记录随 9/4 双平台验证更新）。
- Web：`node --test dashboard/tests/*.test.mjs`（必须 glob 文件）；`python dashboard/serve.py --check`；
  `python scripts/generate_ui_tokens.py --check`。

## 2. 已验证能力清单（9/4）

### Qt 管理端（apps/admin-client）
- 登录：admin/123456（SHA-256 对齐 dev.sql）；空输入/错误密码 1100/服务不可用 均有可见提示与测试。
- 概览：7 日营收 ¥2,865.40、30 日 ¥9,838.40（demo.json 同口径）、桩五态、利用率、UTC 更新时间；
  加载/空/错误/正常四态（StateStack）。
- 桩页：8 列表格（编号/站点/类型/功率/单价/状态/累计次数/时长）；筛选 all/各态/需关注；
  异常联动定位 focusPile；**远程重启（C-S1-005，Mock 模拟）**：仅故障/离线可重启，
  冲突返回协议码 1201 并提示。
- 站页：5 列含在线率（66.7% 演示，同快照聚合，防除零）。
- 用户页：5 列含注册时间 UTC；**冻结/解冻（C-S1-007，Mock 模拟）**，按钮文案随状态切换；
  模拟边界在页面常驻提示中明确（不冒充真实服务端）。
- 主题：昼夜令牌系统，日班默认；`EV_UI_REDUCED_MOTION=1` 停全部动效；导航/布局 1024×700 基准。

### Web 大屏（dashboard）
- 三图表 + 统一地图表面：腾讯地图（有 Key）/ 离线 SVG 拓扑（`?map=topology` 或无 Key 自动）；
- 状态区六类呈现（loading/content/empty/error/offline/stale），阻断/非阻断分级；重试保留最后快照；
- 布局断点 ≥1501 / 901–1500 / ≤900；ECharts 6.1.0 本地 vendor 离线可跑；金额/时间与 Qt 同口径；
- `serve.py --check` 资产门禁通过；密钥只走 config/local.env / 环境变量。

### 测试证据（9/4-9/5 本地与 VM，逐字）
- Windows（构建目录 build/admin-client-socket）：tst_ui **24** / tst_launchsmoke **6** /
  tst_loginflow **7** / tst_socketparse **9** / tst_socketadapter **13**，全 0 failed
- Ubuntu VM（BitDev，qmake6 6.2.4）：同五套逐字一致（tst_socketadapter 曾现 Ubuntu-only
  SIGSEGV——fake server 析构对正在析构的 accepted socket 调 deleteLater 属 UB，已修：
  断开只清 decoder，socket 生命周期交还 QTcpServer；修复后 13/13）
- node：35 passed（Windows + VM）；serve --check / tokens --check 通过

## 3. 本阶段明确未实现项（答辩材料口径，不提前宣称）
- 生产腾讯地图 Key 配置（保留本地联调；无 Key 自动拓扑降级）
- 训练完成的预测/调度模型（`ml` 为第二阶段扩展）
- 真实后端实时推送（管理端统计/桩状态真实 Socket 联调 = 9/7 18:00 闸门；闸门前 Mock 不冒充）
- 冻结/重启动作的**服务端真实执行**（当前为 Mock 模拟，标注清楚）

## 4. 后续更新规则
- 每次功能批合入后在本节登记：日期 + 命令 + 真实 Totals（逐字）；
- Socket 批/闸门结果更新时同步 `docs/meetings/interface-gate-2026-09-07.md`。
