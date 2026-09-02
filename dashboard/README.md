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

- `?map=topology`：强制离线拓扑图（答辩离线演练开关）。
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
