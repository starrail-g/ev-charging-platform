# 接口闸门记录（Interface Gate，2026-09-07 18:00）

> 需求条目：C-S1-020（docs/requirements/README.md）
> 记录人：同学 C（管理端/大屏侧执行与记录）；参与：B（服务端）、A（统筹验收）
> 更新规则：**闸门现场执行后填写**「检查清单」与「结论」，证据可追溯（命令输出/日志文件/PR 链接），不得把 Mock 结果写成真实 Socket 验收。

## 1. 闸门定义

| 项 | 内容 |
|---|---|
| 时间 | 2026-09-07 18:00（北京时间）；同日 17:00–17:10 为官方环境配置测试 |
| 前置冻结 | Q1–Q7 冻结结论（2026-09-05 已冻结：B PR #10 1f157de/11702ae/4eb0bad/45627d5 + 文档样例）→ docs/api/README.md「Q1–Q7 冻结对账」表核对 |
| 通过标准 | B 服务端 `admin.login` / `admin.statistics.get` / 管理端桩状态查询（`pile.list(station_id)` 聚合或 B 增补 `admin.pile.list`）可运行；C 管理端以 `SocketAdminRepository` 完成一次真实登录 + 概览 + 桩列表并展示 |
| 运行方式 | 服务端启动命令（B 提供）；C 侧启动：`EV_ADMIN_DATA_SOURCE=socket ./admin-client`（默认 Mock，见 main.cpp 工厂切换点注释） |

## 2. 检查清单（执行时逐项填写）

> 预跑状态：2026-09-06 已用**真实 main 服务端（`3d015f7`）+ SocketAdminRepository 适配层**
> console 冒烟一轮（非 GUI 展示、非闸门现场），结果与证据已预填下表；闸门现场（9/7 18:00）
> 以管理端 GUI socket 模式逐项复核、更新证据列，并在 §3 填正式结论。
> 证据文件在仓库外 `D:/work/chargingplatform/build/repro/`（不入 git；逐字记录另见
> docs/requirements/current.md §2）。

| # | 步骤 | 结果（通过/失败/阻塞） | 证据（日志/输出/截图） |
|---|---|---|---|
| 1 | 启动 B 服务端（记录启动命令与端口） | 通过（9/6 预跑） | main `3d015f7` 本地构建（build/server-main），dev.sql 种子库，端口 45454；`smoke-live-result.txt` 逐字 |
| 2 | C 管理端 socket 模式登录 `admin/123456` | 通过（9/6 预跑·适配层；GUI 现场复核） | `[PASS] admin.login`（token 已取，逐字见 txt） |
| 3 | 概览 `admin.statistics.get`：7d/30d 营收、五态 counts、利用率、快照时间字段与 C 侧 socketparse 映射一致 | 通过（9/6 预跑；现场复核） | `[PASS] fetchOverview(7d+30d) | revenue7d=3050 revenue30d=3050 idle=2 fault=0 util=0.142 updated=…` |
| 4 | 桩列表 fan-out（或 `admin.pile.list`）字段：`total_charge_count/seconds`、站桩数/在线率（Q7） | 通过（9/6 预跑；现场复核） | `[PASS] fetchStations | stations=2`；`[PASS] fetchPiles(fan-out) | piles=6`；restart 补跑 `request_records` c-admin-33/39 留痕（A-03/B-02 `restart_count:1` → idle） |
| 5 | 错误路径抽查：错误密码 1100、坏帧断连、超时 | 待现场执行 | 9/6 预跑未覆盖真实服务端错误路径（fake-server 测试已覆盖，sa 15/15） |
| 6 | 双平台（Windows 开发机 + Ubuntu VM）各跑一遍 1–5 | 待现场执行 | VM 侧真实服务端冒烟尚未运行 |

## 3. 结论（执行后填写）

- 闸门结果：**通过 / 有条件通过 / 未过**
- statistics.get 状态：可用 / 不可用（若不可用 → 走下方降级流程）
- 遗留问题与责任人：

## 4. 未过闸门处置（协作规则，role-c-delivery-plan §4）

- 管理端按 Mock + 风险标注演示，**材料不冒充真实 Socket 联调**；
- Mock 降级需 **A 书面批准**（评审记录/会议纪要/评论均可），并记入本文件；
- 概览 `statistics.get` 若排期至 9/11 后（第二阶段），阶段一演示明确标注"统计为 Mock 口径"。
