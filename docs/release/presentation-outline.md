# 项目路演 PPT 内容骨架（10 分钟讲解 + 5 分钟提问）

> 材料源（C 汇总版）：正式套用课堂派模板前以此为内容与证据基准；页码与 requirements
> README §2.5 对齐。所有"证据"均为可核实项（提交 SHA / PR # / 测试 Totals），讲解时
> 以 9/10 冻结版录屏与现场演示为准。A 主讲，C/B 支撑。

## P1 封面（15s）
- 项目名：东软电动汽车充电桩应用管理平台（第 X 组）｜成员 A/B/C ｜小学期第一阶段答辩

## P2 项目背景与目标（45s）
- 场景：园区/站前充电运营需要 用户端（预约—充电—结算）+ 管理端（营收/桩态/用户）+ 可视化大屏 的最小闭环
- 第一阶段目标：干净 Ubuntu 可构建、四子系统可启动、一条端到端链路可演示、异常与测试齐全
- 技术栈：Ubuntu + Qt 6 / C++17 / SQLite / Socket(自研 v1 协议) / 原生 Web + ECharts 6.1.0（本地离线）

## P3 系统架构与数据链路（60s）
- 边界图：Qt 用户端 / Qt 管理端 ⇄ Socket 协议 v1 ⇄ 服务端（业务 + libs/database）⇄ SQLite；
  Web 大屏读可追溯统计（demo/数据集），ml 为阶段二扩展
- 金额整数分、时间 UTC ISO-8601、错误码表（0/1000-1003/1100/1101/1200-1202/1300/1500）
- 证据：docs/architecture/*、docs/api/README.md

## P4 数据库与协议（B 讲解，60s）
- schema v0.3（迁移 001/002，migrate_db.py 原子回滚——C-S1-001 复验 ALL PASS）
- 状态机：订单六态、桩五态、frozen 策略（1101）、幂等回放（request_records）
- 证据：database/、libs/protocol/、PR #1/#4；v0.3 迁移 P0 修复 61a9fc3

## P5 用户端演示要点（A 讲解，90s）
- 手机号登录/自动注册 → 附近站点 → 桩详情 → 预约—充电—结算闭环；异常提示（无空闲/余额不足/重复订单）
- 证据：apps/user-client、PR #3；真实 Socket 集成（A-S1-03）闸门状态如实说明

## P6 管理端演示（C 讲解，120s）
- 登录（admin/123456，错误 1100）→ 概览（7d ¥2,865.40 / 30d ¥9,838.40、桩五态、利用率、UTC）→
  桩页 8 列 + 筛选 + 远程重启（仅故障/离线，冲突 1201 演示）→ 站页在线率 → 用户页冻结/解冻（模拟标注）
- 数据层：AdminRepository 抽象 + Mock（演示）/ Socket 适配层（闸门后）；四态（加载/空/错/正常）
- 证据：apps/admin-client、PR #2/#8、tst_ui 24 passed；动作模拟边界与 9/7 闸门状态如实说明

## P7 Web 大屏演示（C 讲解，90s）
- 三图表 + 地图表面：真实地图（本地 Key）/ 离线拓扑（`?map=topology`）；状态区分级；响应式三断点
- 断网演示：本地 ECharts + demo.json（金额/时间/桩态与 Qt 同口径、demo 标识）
- 证据：dashboard/、node 35 passed、serve --check

## P8 测试与质量（C 讲解，60s）
- Qt 三套（tst_ui 24 / launchsmoke 6 / loginflow 7）+ Web node 35，Windows + Ubuntu VM 双平台同绿
- 六类异常覆盖（协议/坏帧/重复/余额/状态冲突/数据库失败）；缺陷闭环 C-S1-001/002 复验记录
- 证据：tests/、docs/release/defect-log.md、docs/requirements/current.md

## P9 分工与贡献（三人，30s）
- A 统筹+用户端；B 服务端/数据库/协议（PRL）；C 管理端/大屏/测试发布（SCML）；互相校验规则
- 证据：GitHub PR #1–#8 列表、docs/requirements/README.md 追溯表

## P10 风险、计划与展望（45s）
- 第一阶段完成度（对照 9/10 清单逐项）；未实现项如实列出（真实统计联调=闸门、ml 扩展）
- 第二阶段安排：反馈修复 → 真实接口全量覆盖 → 数据集/指标增量 → 完整回归（9/17 前）

## 提问预备（5 分钟）
- 为什么管理端动作标注"模拟"：9/7 18:00 接口闸门后 B admin.* handler 就绪才切换 Socket（EV_ADMIN_DATA_SOURCE=socket），材料不冒充联调
- 金额为何整数分：避免浮点误差；展示层 formatYuanCents / formatCents 千分位
- 同口径怎么保证：demo.json ⇔ mockdataset ⇔ 测试三处联动，改一侧必改另两侧（tst 锁值）
- 断电/断网答辩怎么办：离线拓扑 + 本地 ECharts + 固定快照；`?map=topology` 演练已验
