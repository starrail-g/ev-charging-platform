# C 端管理端真实地图渲染·正式实施计划（v1.3，复审修正版）

- 状态：**已实现（T1–T3 代码与测试、T5 文档已完成并本地验证；2026-09-09 工作树全绿，待用户审查后按 §5 commit 边界分批提交；T4 GUI 目检冒烟收尾 + T6 真图联调进行中）**
- 分支：`feature/admin-client-tencent-map`（base origin/main `f5af4a1` = PR #16 merge，开工前已快进）
- 铁律：commit/push 前经用户审查；单分支单 PR；测试声称数 = 实跑 Totals

## 0. 修订记录（v1.0 → v1.1 → v1.2 → v1.3）

### v1.0 → v1.1（首轮评审）

| 评审意见 | 修订落点 |
|---|---|
| P1 取景 zoom 方向相反 | §3.3：改为"**最大**整数 zoom 使 bbox 适配"（zoom↑→地物px↑，取景需向下夹取，原"最小"语义错误） |
| P1 构建工程遗漏 + 接线晚于验收 | §4 重构：**4 个**编译工程（原漏 `tests/ui/ui.pro`，实证见 §4.1）；.pro 注册随每任务即时完成，全量回归独立成 T4 |
| P1 指纹去重阻断失败重试 + resize 无入口 | §3.4 重写：触发点（数据到达/resize 尺寸桶/首次显示）+ 失败清指纹 + 30s 重试门 + 成功指纹去重 |
| P1 析构保护不足（过期响应覆盖） | §3.4：fetch 序号（generation）防 superseded 覆盖（Web P1-01 同构）+ cancelAll + QPointer |
| P2 Provider 失败路径无真实实现测试 | T2：本地假 HTTP 服务器测真实 StaticMapImageProvider 的 121/非 200/超时/坏图路径 |
| P2 坐标系结论超证据 | §1 D4 措辞修正 + §2 证据边界声明；T0b 实测补锁投影公式 |
| P2 输入边界/不可取景行为未定义 | §3.5 输入边界表：空/非法/退化/超大 bbox → 明确行为 |
| 周密性 A：契约闭环 | §4 T5 + §6 门禁：底图例外写回正式协议才可合入 |
| 周密性 B：验收可复现 | §4.1 命令矩阵 + 附录 C（构建/运行/VM/断网测试口径） |
| 周密性 C：完成状态语义 | §4 状态字段：T5=实现完成待联调；T6 通过=完整验收；附录 B 完整 SQL 与执行顺序 |

### v1.1 → v1.2（二轮评审）

| 评审意见 | 修订落点 |
|---|---|
| P1 单轴零跨度退化成单站取景 | §3.3：零跨度轴不参与 zoom 约束，仅两轴全零才走单站 zoom14 |
| P1 generation 须在视图失效时更新 | §3.4：数据/resize/取景变化立即 ++（作废在途），非仅发请求时 |
| P2 D7 仅局部比例非完整算法 | §1 D7 补完整投影式（W=256·2^z、y_world(φ) 精确式；局部比例仅为导数核对值） |
| P2 非法输入回退仍进旧拓扑不安全计算 | §3.5：setStations 入口统一净化，拓扑与取景只消费净化后列表 |
| P2 网络模块依赖与测试隔离未写入任务 | §3.2/§4 T2/T3：provider 显式收 key+baseUrl 不读 env；页面注入；测试命令 env -u；三个测试工程补 QT += network |
| 小修 1 重试测试与时间门一致 | T3：注入时钟用例——30s 内不重试、过门后重试 |
| 小修 2 缓存必须视图匹配 | §3.4：缓存图仅指纹完全匹配才复用，A 图不得承载 B 标记 |
| 小修 3 附录 C 版本/命令/三态/T7 残留 | 附录 C 补版本记录与五套完整命令；T6 补"有 key 上游失败冷启"第三态；全文 T7→T6 |

### v1.2 → v1.3（复审直接修正）

| 评审意见 | 修订落点 |
|---|---|
| P1 图像 Y 轴差值顺序反向 | D7 改为 y_world(lat)−y_world(centerLat)；T1 独立验证北上南下 |
| P2 投影纬度与零尺寸边界缺失 | §3.5 区分业务合法与地图可投影；超地图纬度范围保留拓扑、拒绝真图；非正尺寸不请求 |
| 精确式与导数期望混用 | T1 固定中心纬度 41.72°，精确偏移 −78.052375px，数值容差 0.0001px |
| 取景算例比较量错误 | §3.3：约 355×1.16=411.8px，与请求高度 480px 比较 |
| 同视图刷新反复取消慢请求 | §3.4 先比较目标视图，相同视图复用在途请求；真实失效立即作废 generation |

## 1. 决策基线（已定，勿回退）

| # | 决策 | 出处/依据 |
|---|---|---|
| D1 | 桩/站**业务数据**（含经纬度）全部走 Socket 服务端，零直连 | 现状已满足 |
| D2 | **底图** = 客户端直连腾讯静态图 WebService；渲染层留图片提供者抽象（将来可切服务端代理） | B 2026-09-09 口头明确"地图由管理端自己解决"（无书面留痕，本文件即留痕；补录请求见附录 A） |
| D3 | 凭据只在运行时环境变量 `TENCENT_STATIC_MAP_KEY`，不入 git/日志/截图/DB | 仓库审计红线 |
| D4 | 渲染**不做任何 datum 补偿**。种子坐标 datum 未知（证据只排除 WGS→GCJ 平移形态）；校准后坐标以腾讯解析值（GCJ-02）为准 | T0 证据 + 评审 P2 修正 |
| D5 | 主用 key（WebServiceAPI + IP 白名单 `36.110.14.128` + 已绑配额方案，值仅存运行环境不入仓库）；另一旧 key 弃用 | T0 实测 |
| D6 | 降级语义与 Web MapSurface 同构：无 key/失败/超时 → 自动回退示意拓扑 | 既有 Web 设计 |
| D7 | **投影算法（T0b 实测锁定，完整式）**：静态图 = 中心点窗口，图中心==center 参数，图像 px 与世界 px 1:1。设 W=256·2^z：`x_img = size_x/2 + (lng−centerLng)·W/360`；`y_world(φ) = W/2·(1 − asinh(tan(πφ/180))/π)`；`y_img = size_y/2 + y_world(lat) − y_world(centerLat)`。局部比例（px/°x=W/360、px/°y=W/(360·cosφ)）仅为上式导数、作实测核对用：中心纬度 41.72°、z13 时，局部比例对应每 +0.01°lng 约 58.254px、每 +0.01°lat 的位移绝对值约 78.046px（实测绝对值 58.12/77.30，质心偏置级残差）；图像 Y 轴向下为正，北侧点的 y_img 必须小于图中心，z12 减半；**实现必须用精确式，不得用导数×Δ 近似**（大跨度下两者偏差 >1px） | T0b 证据 |

## 2. T0/T0b 结论对实现的硬约束（全部实测）

- `center` 必填（省略→348）；`size` 用 `640*480` 或 `640x480` 均可（统一 `*`）；`scale=2` 输出 2× 像素（本期不用，已验证）；zoom 12/13 实测（实现夹取 [10,17]，T6 联调复核边界）
- 单图 460–500 KB（640×480）；配额按服务计日；**刷新节流 + 指纹去重必须实现**
- markers 语法（若复用）：样式一次 + 坐标 `|` 续接
- geocoder/place 配额当日易 121：实现与测试零依赖这两者
- dev.sql 种子坐标（站1 `41.7192,123.4315` / 站2 `41.8057,123.4290`）无真实锚点，演示 marker 偏移 1.5/0.9 km——不阻塞代码；T6 演示前按附录 B 处理
- 证据（无 key）：`D:/work/chargingplatform/build/staticmap-probe/T0-evidence.md`（含 T0b 像素锁定与 markers 语法）

## 3. 架构

### 3.1 单控件双模式（对齐 Web MapSurface）

`StationTopologyWidget` 升级双模式，页面层不切换：

- **拓扑模式**：现状绘制一字不改（无 key / 未注入 fetcher / 取景失败 / 拉图失败 / 超时 / 在途）
- **真图模式**：拉图成功后，背景 = 静态图 PNG；投影 = §1 D7 公式；节点/状态色/名字/呼吸 halo/键盘/focus ring/点击信号全复用（只换 m_points 算法与底图）
- **降级标注**：曾尝试真图但失败 → 落回拓扑 + 图例区"地图服务不可用，已回退示意拓扑"（muted）；下次触发点按 §3.4 重试
- 真图模式不画背景网格与站间虚线，隐藏拓扑图例文案

### 3.2 新增文件（`apps/admin-client/src/widgets/`）

| 文件 | 职责 | 关键接口 |
|---|---|---|
| `staticmapviewport.h/.cpp` | 纯函数：取景/投影/指纹 | `struct StaticMapView{centerLat,centerLng,zoom,width,height}`；`bool computeMapView(stations,size,view*)`；`QPointF lonLatToWidgetPoint(lat,lng,view)`；`quint64 requestFingerprint(view)` |
| `staticmapimageprovider.h/.cpp` | 图片获取抽象 + 腾讯实现 | `MapImageProvider`（虚 `fetch(view,seq,cb(seq,ok,img,reason))`、`cancelAll()`）；`StaticMapImageProvider`（QNAM；**构造显式收 `key` 与 `baseUrl`（默认官方端点）——不读环境变量**；key 空则永不发请求；5s 超时；URL/key 仅内存禁打印）；纯函数 `buildStaticMapUrl(key,view)` 供单测 |
| 控件改造 | `stationtopologywidget.{h,cpp}` | 模式枚举/状态 + fetcher 指针 + 底图缓存 + fetch 序号 + paint 分支；测试 accessor 挂 `QT_TESTLIB_LIB`：`renderModeForTest()/mapPointForTest(i)/mapDegradedNoteForTest()/fetchAttemptsForTest()` |

**网络依赖与测试隔离（评审 P2，跨任务约束）**：
- 接线：`OverviewPage` 构造注入一行 `m_topology->setMapImageProvider(new StaticMapImageProvider(qEnvironmentVariable("TENCENT_STATIC_MAP_KEY"), overviewPage))`——**widget 不自建 provider、不读 env**；key 空则 provider 空转（无请求、无网络对象）
- 工程依赖：新 provider 源文件链接 QNAM → `src.pro` + `tests.pro`/`ui.pro`/`loginflow.pro` 三测试工程均需 `QT += network`（socket 两工程已具备，不涉及）
- 测试隔离：任何测试目标不得触真实网络——widget 测试一律注入 Fake；provider 测试显式传 key + `baseUrl` 指向本地假服务器；**CI 与本机测试命令统一 `env -u TENCENT_STATIC_MAP_KEY`**（防开发者机器带 key 时 loginflow 冒烟误发真实请求）
- 现有 tst_ui 拓扑 3 用例零改动即回归锁（无 provider → 拓扑）

### 3.3 取景与投影（公式锁定，见 D7）

- 取景：站集 bbox（只收经纬度合法站，非法站由 §3.5 净化剔除）→ **求最大整数 zoom ∈ [10,17] 使 bbox 两轴 px ×(1+2×0.08) ≤ size 对应轴**；**单轴零跨度（如同经度异纬度）→ 该轴不参与 zoom 约束**（px 需求=0，只按另一轴取景、center=bbox 中心）——**不得退化为单站 zoom14**（会丢掉另一非零跨度轴上可能数公里的站距）；仅当两轴全零（站集同坐标）才走单站规则
- 单站/同坐标站集：center=该站，zoom=14
- 多站：center=bbox 中心
- 找不到适配 zoom（需 zoom<10，超大跨度）→ 返回 false（§3.5）
- 已知向量（写进 T1 单测，坐标=附录 B 校准值）：S1'`(41.714729,123.449597)` S2'`(41.805727,123.440030)`，size 640×480 → center≈`(41.760228,123.444814)`、**zoom=12**（lat 轴绑定：约 355px×1.16=411.8px≤请求高度 480px，故 z12 适配；z13 的约 710px 超出）
- 全站必在图内（取景覆盖全部站，无裁剪设计）；zoom 夹取即防极端拉图

### 3.4 触发、重试、去重与竞态（评审 P1 修订核心）

- **触发点**（任一即评估，debounce 300ms 合并）：
  1. `setStations` 数据到达（含首帧）
  2. `resizeEvent` 后尺寸桶（宽高各按 32px 量化）变化
  3. 降级/失败后距上次尝试 ≥30s（时间门），由下一触发点自然带入
- **去重与缓存语义**：成功 → 存 `(view 指纹, 图)`；触发点评估时**指纹未变且上次成功** → 跳过。**缓存图仅当指纹与当前目标视图完全匹配才可复用**——A 区图已缓存、切到 B 区且 B 拉取失败 → 不得用 A 图承载 B 标记（走拓扑+降级标注）；**失败 → 清成功缓存标记**（不阻断重试），仅受 30s 时间门约束
- **竞态（generation 语义）**：widget 持序号 `m_fetchSeq`；`setStations`/resize 到达时同步净化并计算目标视图，**先比较目标视图，再决定是否失效**。目标视图改变、变为空/不可取景或替换 provider 时，立即 `++m_fetchSeq` 并取消旧在途请求，再进入 debounce；不能等到新 fetch 才作废。同一目标视图仍有请求在途时，保留该请求与序号，不重启 debounce、不取消或重发（仅刷新当前节点业务状态）。发起 `fetch(view,seq)` 后，回调仅当 `seq==m_fetchSeq`、返回对应 view 与当前目标匹配且 widget 未析构才应用；成功时原子保存 `(view, 图)`，按该 view 投影当前站点。析构先使序号失效，再 `cancelAll()`，回调用 QPointer 保护。
- **时间门可测**：30s 时间门用可注入时钟（生产 = QElapsedTimer；测试注入假时钟推进），T3 用例锁定"30s 内不重试、过门后重试"
- 降级语义澄清（测试口径）：**已显示真图后断网不回退**（底图已缓存于 widget，不依赖后续请求）；"降级"只发生在无可用图时的获取失败路径——验证须冷启动或无缓存首请求（附录 C）

### 3.5 输入边界与不可取景行为（评审 P2）

| 输入 | 行为 |
|---|---|
| stations 空 / 全站坐标非法（非有限、越 [-90,90]/[-180,180]） | 净化后为空 → 拓扑模式（现状空态文案不变），**不触发取景/拉图** |
| 混入非法站（合法站 < 全部） | 净化剔除非法站后取景/绘制（拓扑布局同源净化，见下表后注） |
| 业务合法，但任一站纬度超出地图投影范围 `[-85.05112878°,85.05112878°]`（含 ±90°） | 保留在净化后的业务列表供拓扑使用；`computeMapView=false`，不计算该站 Mercator、不请求；标注“坐标超出地图投影范围，已回退示意拓扑”。该范围是本期渲染保护边界，不声称腾讯支持范围已实测 |
| 请求尺寸宽或高 ≤0 | `computeMapView=false`，不投影、不请求；等待后续有效尺寸触发，无地图服务故障标注。边界检查先于单站 zoom14 分支 |
| bbox 退化 | **仅两轴全零（站集同坐标）→ 单站 zoom=14 居中**；单轴零跨度 → §3.3 轴排除规则取景，**不退化单站** |
| bbox 需 zoom<10 才适配（超大跨度） | `false` → 拓扑模式 + 降级标注"区域过大，暂不支持地图视图" |
| 点投影溢出（正常路径不会发生，取景保证全包） | 防御：越界点不绘制标记（log 不打印坐标） |

**统一净化（防旧拓扑不安全计算，评审 P2）**：`setStations` 入口先净化——剔除坐标 NaN/非有限/越界的站，**取景与拓扑布局都只消费净化后列表**（现有 `rebuildLayout` 的 min/max 归一化路径因此不可能吃进非法值产生 NaN 几何）；净化影响 stationCount/点击/键盘映射，T3 加回归用例。净化是 widget 行为变更点，需保持"净化前列表"仅用于对照展示计数的地方语义不变（概览需关注列表按 pile 维度，不受影响）。

业务净化范围仍为纬度 `[-90,90]`、经度 `[-180,180]`；地图投影范围是其后的独立检查，不能把业务合法的极区站静默剔除。`computeMapView` 作为可独立调用的纯函数也须检查输入、尺寸与投影范围，不能依赖 widget 预处理；仅当返回 view 和站点投影均为有限值时才能成功。

## 4. 任务拆解（T1–T6，状态语义：除 T6 外均为"实现完成、待后续验收"）

### 4.1 构建工程矩阵（实证，勿漏）

编译 widget 源文件的目标共 **4 个**（socket 两工程不编 widget，无需注册）：

| 工程文件 | TARGET | 注册时机 |
|---|---|---|
| `apps/admin-client/src/src.pro` | admin-client | 每任务新增源文件即时注册 |
| `apps/admin-client/tests/tests.pro` | tst_launchsmoke | 同上 |
| `apps/admin-client/tests/ui/ui.pro` | tst_ui | 同上（v1.0 遗漏项） |
| `apps/admin-client/tests/loginflow/loginflow.pro` | tst_loginflow | 同上 |

（顶层 `admin-client.pro` 为 SUBDIRS 聚合，无需改；.pro 与目录同名规则、qmake SUBDIRS 坑见 skill。）

### T1 纯函数 core（viewport/投影/指纹）
- 文件：`staticmapviewport.{h,cpp}`；**即时注册 4 工程**（§4.1）
- 单测（tst_ui 新增，名草案）：`viewportDemoPairLocksToZoom12AndCenter`（§3.3 已知向量）/ `viewportSingleStationFallsBackToZoom14` / `viewportZeroSpanAxisExcludesThatAxisFromZoomConstraint`（同经度异纬度两站：zoom 按 lat 轴取、center=中点——**非单站退化**，评审 P1）/ `viewportEmptyOrAllInvalidReturnsFalse` / `viewportSpanTooLargeReturnsFalse` / `viewportSkipsInvalidStations` / `projectionCenterMapsToImageCenter` / `projectionKnownOffsetsMatchLockedFormula`（固定中心 (41.72°,123.44°)、size=640×480：z13 +0.01°lng→+58.254222px、+0.01°lat→−78.052375px，绝对容差 0.0001px；z12 两者减半；期望为精确式数值，非导数近似）/ `projectionExactFormulaDiffersFromDerivativeApproximationOnWideSpan`（±0.045° 跨度断言全式与导数近似的差异 >0 且实现按全式——防偷用近似，评审 P2）/ `fingerprintStableAndChangesOnViewChange`
- 验收：单测绿；零网络；现有控件行为零改动（拓扑 3 用例绿）
- 边界与方向补充单测：`projectionNorthIsAboveAndSouthIsBelowCenter`（独立断言北侧 y<中心、南侧 y>中心，不调用被测投影生成期望）；`viewportRejectsPolesAndOutOfProjectionRange`（±90°及地图范围外均失败；边界内/边界上单站成功且 view 与点有限）；`viewportRejectsNonPositiveSize`（零宽、零高、负尺寸，即使单站也失败）。

### T2 图片提供者
- 文件：`staticmapimageprovider.{h,cpp}`；注册 4 工程；**三测试工程补 `QT += network`**（§3.2 网络依赖节；socket 两工程已有，不涉及）
- 单测：`urlBuilderMatchesProbeParameters`（`*` 分隔/center/zoom/size 键值）/ `providerNoKeyShortCircuitsWithoutNetwork`（显式空 key）/ **`providerMapsQuotaErrorToReason`（本地 QTcpServer 假 HTTP 回 status 121 JSON → reason=quota，URL 不出现在任何日志/错误串）** / `providerHandlesHttpErrorAndTimeout`（假服务器 500/静默超时）/ `providerRejectsNonImageBody` / `providerCancelAllStopsDelivery`——全部显式传 key + baseUrl=本地假服务器，**零真实网络**（评审 P2 隔离）
- 验收：真实实现失败路径全覆盖（首轮 P2 闭合）；测试目标在 CI 与本机均不触外网（命令层 `env -u` 见附录 C）

### T3 控件双模式（核心）
- 范围：`stationtopologywidget.{h,cpp}` 双模式 + §3.5 净化 + **OverviewPage 接线**（§3.2 注入 provider，页面唯一改动点）；tst_ui 新增用例：
  - `topologyModeWhenNoProviderKeepsLegacyBehavior`（回归锁）
  - `mapModeRendersProjectedPointsFromFakeImage`（Fake 成功：模式=Map、`mapPointForTest`==T1 投影期望、底图已存）
  - `mapModeKeepsClickAndKeyboardActivation`
  - `degradedFallsBackToTopologyWithNote`（Fake 失败）
  - `retryBlockedWithinTimeGateThenAllowedAfterClockAdvance`（注入时钟：失败→立即刷新 fetch 计数不变；推进 ≥30s 再刷新 → 新 fetch——时间门用例，小修 1）
  - `staleRegionCacheNotReusedForDifferentViewport`（A 视图成功缓存 → 切 B 视图且 B 失败 → 拓扑+降级标注，底图不得为 A 图——缓存匹配，小修 2）
  - `supersededResponseAfterViewInvalidationIsDiscarded`（真实目标变化、清空、变为不可取景或替换 provider 后，旧响应在 debounce 窗口内或之后返回均丢弃）
  - `sameViewportRefreshKeepsInFlightRequest`（慢请求在途，重复刷新相同坐标或仅状态改变：fetch/cancel 计数不增加，原响应仍可应用）
  - `outOfProjectionRangeStationRemainsInTopology`（业务合法极区站保留 stationCount/键盘/点击映射，禁止拉图，拓扑布局有限）
  - `resizeBucketChangeTriggersRefetch`（resize 后尺寸桶变化 → Fake 收到新请求）
  - `degradedNoteClearsOnNextSuccessfulFetch`
  - `invalidStationCoordinatesSanitizedBeforeLayout`（NaN/越界站剔除：拓扑绘制不崩、stationCount 只计净化后、取景不含非法站——评审 P2）
- 验收：用例绿；降级/重试/竞态/缓存/净化五类路径全有锁；页面接线后 loginflow 冒烟（T4，`env -u` 跑）

### T4 全量回归与集成冒烟
- 内容：4 工程重建（附录 C 命令）+ Windows 五套全量：ui（24+新增）/launchsmoke 6/loginflow 7/socketparse 10/socketadapter 16（基线=origin/main，实跑 Totals=声称数）；`EV_UI_REDUCED_MOTION=1` 复跑 tst_ui；全部测试命令带 `env -u TENCENT_STATIC_MAP_KEY`
- 集成冒烟：Mock 模式起 GUI（无 key）→ 概览页拓扑行为与现状逐项一致（目检清单：节点/呼吸/键盘/点击）
- 状态：实现完成、待 T6 联调

### T5 文档批（状态=实现完成、待 T6 联调）
- `docs/ui/README.md` 地图节：双模式/降级标注/key 环境变量（与 `TENCENT_MAP_KEY` 大屏 JS key 的类型区别）/D7 公式与 T0b 证据引用
- `config/example.env`：空值 `TENCENT_STATIC_MAP_KEY` + 注释（WebService 型、IP 白名单、禁入 git）
- **契约闭环（周密性 A）**：`docs/architecture/map-service-protocol.md` §1 补底图例外（文本=附录 A 下半）；方式：请求 B 合入或经 B 评审由本 PR 附带（见 §6 门禁）
- `current.md` 三块式刷新；本文件状态翻"已实现"
- 验收：README 与实现逐字一致；无 key 泄漏；两文档无矛盾

### T6 真图联调（通过后=完整验收）
- 前置：附录 B 坐标通道任一完成
- Windows：全新库起 ev-server → 管理端 socket + key → 目检真图+两站标记贴合 → **降级验证冷启动三态**：①无 key 冷启 = 拓扑（静默，无标注）②带 key 网络正常冷启 = 真图 ③**带 key 但上游失败（无效 key/断网）冷启 = 拓扑 + 降级标注**——运行时断网不清缓存≠降级（已显示真图不回退），勿作降级测试法 → 截图证据落 `D:/work/chargingplatform/build/`（外部路径）
- Ubuntu VM：同配方 GUI 联调（guest 出网预检 `curl -s -o /dev/null -w '%{http_code}' 'https://apis.map.qq.com/ws/staticmap/v2/'`——NAT 出口=宿主 `36.110.14.128` 已白名单，但需实测）+ 五套复跑双平台计数一致
- 验收：用户目检通过 + 证据文件 + 双平台一致

## 5. 交付纪律与 commit 边界

- 单 PR（base 当前 origin/main `f5af4a1`）；commit 边界 = T1→T2→T3→T4→T5（文档）；跨批同文件仅 `stationtopologywidget.*`（T3）与 4 工程 .pro（随任务即时、T3 后不再动）；提交前 `git diff --unified=0` 复核
- PR #16 已合 main（f5af4a1）——§4 T4 基线为 ui 34（非文档 v1.2 时点 24），本批完成后 ui 65
- 禁 `git add -A`（工作树有用户遗留 `docs/meetings/interface-gate-2026-09-07.md` M 状态，勿卷入）
- 证据引用：外部绝对路径 + 先逐字核对再写 PASS

## 6. 合入门禁（§0 周密性 A）

本 PR 合入 main 前须同时满足：
1. 五套全量绿（双平台，T6 后）
2. 协议补录已合入 main **或** 本 PR 附带的协议文档修改经 B 评审通过（附录 A 文本）
3. `docs/ui/README.md` 与实现逐字一致
4. T6 目检证据落盘

## 7. 风险与依赖

| 风险 | 影响 | 缓解 |
|---|---|---|
| 演示机出口 IP 变动 → 110 | T6 | 换绑步骤入 README；T6 前预检 |
| 配额日耗尽 → 121 | 真图偶缺 → 自动拓扑（可演示降级） | 指纹+节流；演示当日早间预检 |
| 种子坐标未校准 | marker 偏移 1.5/0.9km | 附录 B 双通道 |
| PR16 合入时序 | .pro 冲突 | §5 预案 |
| 投影残差 >±1px 场景（高分/大 zoom） | 标记微偏 | T6 目检闸门；残差量级已实测锁定 |
| B 不配合协议补录 | 门禁 2 挂起 | 附录 A 已备文本；门禁保证不静默合入矛盾文档 |

## 8. 非目标（本期不做）

桩页/站页地图化；平移缩放与瓦片；`scale=2` 高分适配（已验证留后续）；服务端底图代理（B 划归客户端）；Web 大屏收编；等待 v0.4 服务端能力。

## 附录 A：给 B 的消息（合并为一条转发）

> ①种子校准：T0 实测站1/站2 种子与各自 address 腾讯解析点差 1582/914 m（证据 `D:/work/chargingplatform/build/staticmap-probe/T0-evidence.md`，S1 两次复现）。建议 dev.sql 两站坐标改为 geocode 自洽值：站1→`41.714729,123.449597`、站2→`41.805727,123.440030`（或确认种子来源意图）。②协议补录：PR #15 §1 "Clients do not call Tencent directly…" 未覆盖底图渲染层，且 9/9 已确认底图由客户端自理。建议 §1 补：
> "Base-map rendering (static imagery / SDK / tiles) is a client-side concern and is outside the server map service scope; only map-data and business payloads travel through the Socket protocol. Client-side base-map keys are deployment configuration and must never enter Git, logs, or database."
> 同意则 B 自行补录，或授权 C 随地图 PR 附带（B 评审）。

## 附录 B：演示坐标通道与完整 SQL（周密性 C）

- 通道一（正式）：B 校准 dev.sql 合 main → 之后新建库即新种子
- 通道二（不等待 B）：本地联调库操作（**顺序不可颠倒**）：
  1. 确认无旧库：`ls <EV_DATABASE_PATH 目标>` 不存在才继续（已存在旧库 → 换新文件名）
  2. 起 ev-server（自动建库 + v0.3 种子）
  3. 校准：
     ```sql
     UPDATE stations SET latitude=41.714729, longitude=123.449597 WHERE id=1;
     UPDATE stations SET latitude=41.805727, longitude=123.440030 WHERE id=2;
     ```
  4. 复核：`SELECT id,name,latitude,longitude FROM stations;`
  5. **约束：此后不得删除/重建该库**（重建会重新落旧种子）；若通道一已合 main，跳过 3），2) 后直接复核新坐标
- 证据：`D:/work/chargingplatform/build/staticmap-probe/T0-evidence.md`

## 附录 C：构建/运行命令矩阵（可复现，v1.2）

**版本记录（每次回归证据文件首行写入）**：Windows 用 `D:/Qt/6.2.4/mingw_64/bin/qmake.exe`（输出 `qmake 3.1 / Qt 6.2.4`）；Ubuntu 用 `qmake6`（Qt 6.x）。**两者同为 Qt6 qmake——仓库/CI 文档泛称 "qmake6" 指 Qt6 qmake，Windows 本机 6.2.4 即其对应物**；`qmake --version`/`qmake6 --version` 输出必须逐字记入证据，不得省略。

Windows（git-bash；PATH 规则与坑见 skill:ev-charging-platform）：
```bash
export PATH=/usr/bin:/mingw64/bin:/c/Python314:/d/Qt/6.2.4/mingw_64/bin:/d/Qt/Tools/mingw1310_64/bin:$WINDIR/system32
qmake --version                       # → 证据首行
cd D:/work/chargingplatform/build
qmake D:/work/chargingplatform/ev-charging-platform/apps/admin-client/admin-client.pro && mingw32-make -j4
# 五套完整运行（-o 结果文件一律 D:/ 原生路径；产物实际位置以 find 为准，勿凭记忆）：
export QT_QPA_PLATFORM=offscreen
env -u TENCENT_STATIC_MAP_KEY <build>/tests/ui/tst_ui.exe            -o D:/work/chargingplatform/build/out-ui.txt,txt
env -u TENCENT_STATIC_MAP_KEY <build>/tests/tst_launchsmoke.exe      -o D:/work/chargingplatform/build/out-ls.txt,txt
env -u TENCENT_STATIC_MAP_KEY <build>/tests/loginflow/tst_loginflow.exe -o D:/work/chargingplatform/build/out-lf.txt,txt
env -u TENCENT_STATIC_MAP_KEY <build>/tests/socket/tst_socketparse/tst_socketparse.exe   -o D:/work/chargingplatform/build/out-sp.txt,txt
env -u TENCENT_STATIC_MAP_KEY <build>/tests/socket/tst_socketadapter/tst_socketadapter.exe -o D:/work/chargingplatform/build/out-sa.txt,txt
# <build> = D:/work/chargingplatform/build；逐文件读 txt 取 Totals，禁止只看 exit code
```
Ubuntu VM（guest，qmake6；.pro 传源树绝对路径；vmrun 证据规则见 skill）：
```bash
qmake6 --version                       # → 证据首行
cd <guest 构建目录> && qmake6 <源树>/apps/admin-client/admin-client.pro && make -j4
# 五套同法（offscreen + -o /tmp/*.txt 落 guest，再 copyFileFromGuestToHost 拉回逐字比对）
```
验证口径（skill 铁律）：用例数双平台逐字一致 + exe mtime 晚于源文件 + 新用例名在场，三者齐证才可信。
