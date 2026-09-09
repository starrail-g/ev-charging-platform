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
  Qt 用 `kMotion*PulseMs` 常量、Web 用 theme-tokens 的 motion 组。概览营收卡（`RevenueMetricCard`）
  近 7/近 30 切换的字号动画取 **panel 200ms** 令牌（`QVariantAnimation` 单点驱动图表 viewport；
  只动字号/透明度视觉属性，不给金额插值、不伪造曲线中间数据）。
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
| 营收 | 概览营收卡 = `RevenueMetricCard`（近 7/近 30 两行金额切换 + Mini 折线 + 详情入口）；销售业绩页 `RevenuePage`（合计卡 + Full 趋势图 + 每日营收表） | charts.js 营收趋势（7d/30d 切换） |
| 桩五态 | 桩页表格 8 列 + StatusTag | 状态分布图 + 态势图站点着色 |
| 桩级异常 | 需关注列表 + 故障/离线呼吸脉冲 | 告警联动（badge/图表），点击联动 |
| 站点 | 站页 5 列（在线率） | 地图 + 利用率排行 |
| 用户 | 用户页 5 列 + 冻结/解冻（Mock 模拟标注） | — |
| 数据口径 | MockAdminRepository/mockdataset | data-adapter/demo.json（金额整数分、UTC、五态同源） |
| 管理动作 | restartPile / setUserStatus（模拟或 Socket） | —（大屏只读） |

- 数据链路：页面 → `AdminRepository` 抽象（Mock 500ms / Socket 适配层）→ 展示；页面不建 Socket
  不写 SQL；Mock 与 demo.json 数据同口径（改一侧必须同步另一侧 + 双端测试）。
  2026-09-08 起营收 30 日序列亦同口径：`mockdataset.cpp` 的 `kRevenueCents30` 与
  `dashboard/data/demo.json` `revenue30dCents` 逐值一致（30 元素和 = 983840 分；末 7 位之和 =
  7d 合计 = demo.json `revenueCents` 286540 分），由 tst_ui `mockRevenueSeriesAreConsistent` 锁定；
  运行时不互相读取。

## 6. 管理端页面清单（2026-09-04 建立；2026-09-08 增补营收导航与销售业绩页）

| 页面 | 文件 | 状态 |
|---|---|---|
| 登录页 | `apps/admin-client/src/pages/loginpage.*` | Mock 登录全流程 + 错误分支测试；Socket 待闸门 |
| 概览页 | `apps/admin-client/src/pages/overviewpage.*` | Mock 四态 + 第四张营收卡 = `RevenueMetricCard`（近 7/近 30 切换，详见 6.1）；真实统计待闸门 |
| 销售业绩页 | `apps/admin-client/src/pages/revenuepage.*` | 2026-09-08 新增（feature/admin-revenue 本地产物）：合计卡 + Full 趋势 + 每日营收表 + 更新时间 |
| 充电桩页 | `apps/admin-client/src/pages/pilepage.*` | 8 列/筛选/聚焦 + 重启按钮（C-S1-005 Mock 模拟） |
| 充电站页 | `apps/admin-client/src/pages/stationpage.*` | 5 列含在线率 |
| 用户管理页 | `apps/admin-client/src/pages/userpage.*` | 5 列 + 冻结/解冻按钮（C-S1-007 Mock 模拟） |

动作模拟边界：重启/冻结成功文案带"（模拟）"后缀，正常态页脚常驻提示"9/7 接口闸门后接入真实
服务端"；状态栏数据来源标识来自 Repository（Mock 演示 / Socket(host:port)），不硬编码。
2026-09-08 起概览顶部"演示控制"（数据模式下拉）仅 **Mock 数据源**下显示，Socket 模式隐藏——
错误文案统一为"接口错误：概览加载失败"，不以演示字样冒充真实接口结果。

### 6.1 概览营收卡与图表层级（2026-09-08 视觉决策，用户确认）

**导航与页面结构**：主导航五项 = 概览 / 销售业绩 / 充电桩 / 充电站 / 用户管理；导航列表项间距
`spacing=18`（原 14 的 1.3 倍，用户指定，QSS 项高 52px），垂直滚动条 `ScrollBarAsNeeded`；
页面索引用命名枚举 `PageIndex`（`OverviewIndex`/`RevenueIndex`/`PileIndex`/`StationIndex`/
`UserIndex`，替换裸数字）。概览营收卡标题行"详情"入口携带**当前 7/30 范围**跳转销售业绩页并预选
该范围；登出/切身份时双页作废在途请求（`invalidatePendingLoads()`，旧 generation 回包丢弃）。

**营收卡（`RevenueMetricCard`，对象名 `revenueCard`）交互**：近 7 日 / 近 30 日两行金额，
各自为透明热区按钮（点击/键盘/tooltip/焦点环），点击切换主次金额（200ms panel 字号动画，只动视觉
属性、金额不插值）；选中序列不可用 → 折线区显示"趋势暂不可用"+ 重试入口，金额行与其它摘要保留。

**图表层级（Mini 模式 = 概览卡内折线；Full 模式 = 销售业绩页主图）**，自下而上：

1. 背景层：Mini 折线 = 真实比例数据，以 **60% 透明 `kDayFocusBlue`**（日班焦点蓝 `#0E6E8C`，主题令牌）
   绘制，作为低透明度背景纹理——**折线不压文字**；轻量坐标网格（底轴/纵轴/3 条水平线）手绘于内容
   下层，结构色 45%~65% 不透明度；Mini 轴对象整体隐藏，使 plotArea≈视口（消除隐藏轴占位造成的折线
   内缩）。
2. 聚焦带：Mini 的 Y 量程聚焦数据带（数据上下各留 15% 余量），折线在卡内**垂直居中**。
3. 最上层：金额文字由宿主经 `drawForeground` 前景回调绘制在一切内容之上——主金额 ~92% 近实色正文、
   次金额 85% `mutedText`（`#5F7068` 弱化文本令牌），保证数据可读。

**Full 模式（销售页）与 Mini 的差异**：Y 轴自 0（真实零基线，金额仅在绘图边界由分转元）；横轴用
UTC 儒略日数值定位（不受本机时区影响），日期标签稀疏——7 日逐日 / 30 日约 6 个含首尾；悬停按 X 位置
找回 `RevenueDay`、用原整数分格式化精确金额（不从浮点反算）。Mini 不做逐点 hover（点击语义由热区承担）。
非法/空序列由调用方先判 `available`；图表自身 `clearSeries` 一次清空数据/点/提示/回调。

### 6.2 概览站点态势：真图底图渲染（2026-09-09，T3 已实现）

**单控件双模式**：`StationTopologyWidget`（对象名 `stationTopology`）同一画布承载两种模式，
页面层不感知切换——

- **拓扑模式（默认）**：min/max 归一化示意拓扑，现状绘制一字不改。触发条件 = 未注入提供者 /
  key 为空 / 净化后无站 / 控件未布局 / 取景失败（坐标超投影范围、区域过大）/ 拉图失败 / 超时 /
  请求在途。拓扑模式保留背景网格、站间虚线、图例"态势示意，不代表物理电网连接"。
- **真图模式**：拉图成功后，背景 = 腾讯静态图 PNG，节点/状态色/站名/呼吸 halo/键盘/点击/
  focus ring 全部复用，仅投影换用 Web-Mercator 精确式。真图模式不画网格与站间虚线，隐藏图例。
- **降级标注**：曾尝试真图但失败/不可取景 → 落回拓扑，图例区显示标注（替换示意声明）：
  拉图失败 = "地图服务不可用，已回退示意拓扑"；站纬度超投影范围 = "坐标超出地图投影范围，
  已回退示意拓扑"；跨度过大 = "区域过大，暂不支持地图视图"。失败后 30s 时间门内不重试
  （下一触发点自然带入）；已显示真图后断网**不回退**（底图已缓存，不依赖后续请求）。

**接线与凭据**：`OverviewPage` 构造注入
`new StaticMapImageProvider(qEnvironmentVariable("TENCENT_STATIC_MAP_KEY"))`（所有权转移给
控件；控件不自建 provider、不读 env）。key = **WebServiceAPI 型**（`config/example.env` 的
`TENCENT_STATIC_MAP_KEY`）：授权方式必须是 **IP 白名单**（桌面/脚本无 Referer，域名白名单报
status 110），且须**手动绑定配额方案**（未绑方案每日仅 1 次体验额度，报 status 121）；与
`TENCENT_MAP_KEY`（Web 大屏 JS API key、域名白名单）类型不同、语义隔离，勿混用。空 key =
provider 空转（不建网络对象、不发请求）→ 拓扑模式。URL/key 仅内存构造，禁打印/落盘。

**渲染与数据流（分层纪律）**：底图 = 客户端直连腾讯静态图 WebService（渲染层经
`MapImageProvider` 抽象，将来可切服务端代理）；桩/站业务数据（含经纬度）**仍全部走 Socket
服务端**，零直连——真图只换概览拓扑画布的底图与投影，数据流零改动。投影公式（D7 精确式，
见 `docs/role-c-admin-map-renderer-plan.md` §1）：`x = w/2 + Δlng·W/360`；
`y = h/2 + y_world(lat) − y_world(centerLat)`，`W = 256·2^zoom`、
`y_world(φ) = W/2·(1 − asinh(tan(πφ/180))/π)`（实现禁导数近似）。不做 datum 补偿；
坐标校准后以腾讯解析值（GCJ-02）为准。T0/T0b 实证与像素证据（无 key）：
`D:/work/chargingplatform/build/staticmap-probe/T0-evidence.md`（仓库外路径）。

**触发/去重/竞态（与 Web MapSurface 同构语义）**：触发点 = 数据到达（setStations）/
resize 后 32px 尺寸桶变化，debounce 300ms 合并；成功图按（center 5dp + zoom + 尺寸）指纹
缓存，**仅指纹完全匹配才复用**（A 区图不得承载 B 区标记）；同视图请求在途 → 复用不重发；
目标视图改变/清空/不可取景/替换提供者 → fetch generation 立即作废并取消旧请求，迟到的旧
响应不落地（QPointer + seq 双保险）。净化：setStations 入口剔除坐标非法站（NaN/越业务范围），
拓扑布局、取景、stationCount、点击/键盘都只消费净化后列表；业务合法但纬度超
±85.05112878°（地图投影保护边界）的站保留在拓扑列表，仅禁止上图。请求图尺寸 = 控件尺寸
32px 向下桶化并夹取 [320,1280]×[240,960]。

**测试**：tst_ui 新增 36 用例（T1 纯函数 13 + T2 provider 6 + T3 双模式 12 + 静态审查回归 5，
全部走本地假服务器/Fake 提供者，零真实网络、`env -u TENCENT_STATIC_MAP_KEY` 运行）；既有拓扑 3
用例零改动回归锁（无 provider → 拓扑）。取景/投影已知向量与实现决策 → 实施计划 §3.3/§4。

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
