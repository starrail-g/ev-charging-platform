# 充电网络实时态势大屏（dashboard）

Web 实时态势大屏：原生 HTML/CSS/ES modules + 本地 Apache ECharts（离线可运行），
数据与 Qt 管理端 Mock 同一口径（`apps/admin-client` 的 `mockdataset`）。

## 本地启动

Windows（PowerShell）：

```powershell
Copy-Item config/example.env config/local.env   # 首次；local.env 已被 .gitignore
# 可选：编辑 config/local.env 设置 TENCENT_MAP_KEY；不设置则自动使用离线拓扑图
python dashboard/serve.py --port 61469
```

Ubuntu：

```bash
cp config/example.env config/local.env
python3 dashboard/serve.py --port 61469
```

打开 http://127.0.0.1:61469/ 即见主屏。

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

- `config/local.env` 已被 gitignore，真实 `TENCENT_MAP_KEY` 永不进入仓库、日志、
  截图或测试快照；环境变量 `TENCENT_MAP_KEY` 优先于本地文件。
- 服务日志不打印配置、请求响应体或 key。
- 未配置 key 时页面不发起腾讯地图请求，自动降级为拓扑图。

## 本地化依赖（vendored）

| 文件 | 说明 | SHA-256 |
|---|---|---|
| `vendor/echarts.min.js` | Apache ECharts 6.1.0（Apache-2.0） | `b66b25aeb4df84e33199dc21694014d336d222cbd9deb0e5a7c14bd6aa0d0fd0` |
| `vendor/LICENSE.echarts.txt` | ECharts Apache-2.0 许可证原文 | `634293835b43a6dd2094fa39182a3d9a6b9ca43b7fdb9ac354e8037af2a3093a` |
