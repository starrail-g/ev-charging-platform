# 充电网络实时态势大屏（dashboard）

Web 实时态势大屏：原生 HTML/CSS/ES modules + 本地 Apache ECharts，
正常运行读取第一阶段 Schema v0.4 SQLite 快照；`demo.json` 仅作为离线演练 fixture。

二阶段分析区和主态势数据通过 `EV_ANALYSIS_API_BASE_URL` 读取独立 Flask 服务：主地图/桩/营收快照来自 Schema v0.4 SQLite，预测、推荐和预警来自 ADS 模型产物；服务不可用时只显示明确的降级状态，不伪造结果。`demo.json` 仅保留离线故障演练，不是最终呈现数据源。

## 本地启动

Windows（PowerShell）：

```powershell
Copy-Item config/example.env config/local.env   # 首次；local.env 已被 .gitignore
# 可选：编辑 config/local.env 设置 TENCENT_MAP_JS_KEY；不设置则自动使用离线拓扑图
python dashboard/serve.py --port 61469
```

Ubuntu：

```bash
cp config/example.env config/local.env
python3 dashboard/serve.py --port 61469
```

打开 http://127.0.0.1:61469/ 即见主屏。

## 二阶段工作台导航

工作台与一阶段客户端页面独立，采用左侧导航切页：`网络总览`、`智能预测`、`用户与设备`、`订单与能源`、`收益与站点`、`评价与服务`。每个分析页只组合同一业务域的相关图表，并在图表上方显示快照 KPI；导航状态同步到 URL hash（例如 `/#forecast`），可直接分享某个分析视角。宽度小于 1180px 时导航自动转为横向滚动栏，宽度小于 720px 时分析卡片单列排列。

分析服务启动示例（另一个终端）：

```bash
EV_ANALYSIS_ARTIFACT_DIR=/path/to/ads \
  flask --app ml.service.app run --host 127.0.0.1 --port 61501
```

然后在 `config/local.env` 设置 `EV_ANALYSIS_API_BASE_URL=http://127.0.0.1:61501`，重启大屏服务。

## 数据挖掘模块

每个工作台页面固定呈现至少四个模块，模块标题同时标注方法与数据口径，便于答辩或运营复核：

- 网络总览：加权健康评分、设备 z-score 异常扫描、站点利用率/能耗聚类、四时段峰谷分解。
- 智能预测：Spark MLlib 随机森林回归、模型 MAE/RMSE 验证、预测值与残差口径说明、推荐与历史 P95 预警。
- 用户与设备：订单频次分布、RFM 用户价值矩阵、桩状态分布、功率档位与异常桩联合画像。
- 订单与能源：订单转化时序、状态漏斗、小时能耗/订单双轴、时段能耗与 Pearson 相关系数。
- 收益与站点：30 日营收趋势、站点 Pareto 排行、OLS 趋势拟合、聚类中心与累计营收集中度。
- 评价与服务：完成/非取消/复购代理指标、履约时长分布、日完成率控制图、评价事实完整性。

上述指标均由 Schema v0.4 快照或 ADS 模型产物派生。Schema v0.4 没有评价事实表时，页面显式显示“评价事实缺失”，不会生成虚构星级或情感分数。图表启用 ECharts 的 `aria` 描述能力，并使用 tooltip、visualMap/标线和清晰单位支持多维探索；交互设计参考 [ECharts 交互能力说明](https://echarts.apache.org/en/feature.html) 与 [可访问性指南](https://echarts.apache.org/handbook/en/best-practices/aria/)。

## 演示参数（验收用）

- `?map=topology`：强制离线拓扑图（答辩离线演练开关；无密钥时本来就自动降级）。
- `?state=empty|error|offline|stale`：本地演示状态注入——只改变页面呈现分支，
  状态区始终带"演示状态注入"标识，不伪装成真实接口结果；正常 URL 不受影响。
  - `empty`：阻断式空态（主区隐藏，显示"暂无数据 + 重试"）。
  - `error`：阻断式错误态（显示模拟失败原因与重试按钮）。
  - `offline`：非阻断横幅"离线模式"（主体保留最后有效快照，地图强制拓扑）。
  - `stale`：非阻断横幅"数据可能已过期"（显示最后有效快照 + 刷新按钮）。
- 页面级状态区（header 下方）：loading / content / empty / error / offline / stale 六类
  统一呈现；阻断态占满主区，非阻断态为细横幅并压缩紧凑态主区高度。
- 加载失败后点"重试"直接重跑 `loadDashboard()`，保留最后有效快照，不做整页刷新。

## 布局断点（与 2026-09-03 修复验收一致）

- ≥1501px：三栏全景（左指标 / 中央地图 / 右栏）+ 底部三图并列。
- 901–1500px：紧凑态——指标横条占顶行，地图左、右栏右，底部单图页签（默认 24h）。
- ≤900px：单列纵向信息流，自然滚动。
- `python dashboard/serve.py --check`：不启动服务，校验答辩必需资产是否存在。

## 密钥安全边界

- Dashboard 只读取独立的 `TENCENT_MAP_JS_KEY` 用于 GL 底图；服务端 WebService
  `TENCENT_MAP_KEY` 永不下发到浏览器。真实密钥均已被 gitignore，永不进入仓库、日志、
  截图或测试快照；环境变量 `TENCENT_MAP_JS_KEY` 优先于本地文件。
- 服务日志不打印配置、请求响应体或 key。
- 未配置 key 时页面不发起腾讯地图请求，自动降级为拓扑图。

## 本地化依赖（vendored）

| 文件 | 说明 | SHA-256 |
|---|---|---|
| `vendor/echarts.min.js` | Apache ECharts 6.1.0（Apache-2.0） | `b66b25aeb4df84e33199dc21694014d336d222cbd9deb0e5a7c14bd6aa0d0fd0` |
| `vendor/LICENSE.echarts.txt` | ECharts Apache-2.0 许可证原文 | `634293835b43a6dd2094fa39182a3d9a6b9ca43b7fdb9ac354e8037af2a3093a` |
