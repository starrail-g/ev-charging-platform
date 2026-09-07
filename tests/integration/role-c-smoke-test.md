# 角色 C 冒烟与联调步骤文档（C-S1-021）

> 计划执行日：9/8（随发布候选版联调执行）。
> 范围：管理端（apps/admin-client）+ Web 大屏（dashboard）在**本地/开发机**的可重复启动与冒烟路径；
> 干净 Ubuntu 环境构建另见 C-S1-025 与 stage1-checklist。
> 更新规则：执行后把实际结果与证据写入「执行记录」表；命令与当前代码版本一致（9/9 冻结前如有变更需同步刷新）。

## 1. 前置

- 仓库：`D:\work\chargingplatform\ev-charging-platform`（Windows 开发机）或 `~/ev-charging-platform`（Ubuntu VM）
- 构建产物在仓库外构建目录（不提交）；Qt 三套测试 exe 见回归文档 C-S1-022

## 2. 管理端冒烟路径（Mock 模式，默认）

1. 构建（Windows，git-bash）：
   ```bash
   export PATH="/c/Python314:/d/Qt/6.2.4/mingw_64/bin:/d/Qt/Tools/mingw1310_64/bin:/c/Windows/System32:/c/Windows"
   cd D:/work/chargingplatform/build/admin-client-ui && mingw32-make -j2
   ```
   Ubuntu：`qmake6 apps/admin-client/admin-client.pro && make -j2`（构建目录仓库外）
2. 启动：`QT_QPA_PLATFORM=offscreen` 仅测试用；真实演示直接运行 `admin-client`（Windows exe / Linux 可执行文件）
3. 冒烟步骤：
   - 登录页：空输入校验 → `admin/123456` 登录成功进入业务区
   - 概览页：营收（¥2,865.40 / 30 日 ¥9,838.40）、桩五态、利用率、更新时间可见
   - 充电桩页：6 桩 8 列（含累计次数/时长）；筛选"需关注"=2；选中故障桩 P-101-C → **重启选中桩** → 状态转空闲且提示行可见（模拟操作标注）
   - 充电站页：2 站 5 列，在线率 66.7%
   - 用户管理页：3 用户 5 列（注册时间 UTC）；选中正常用户 → **冻结** → 状态"冻结"；再 **解冻** 回"正常"
   - 退出登录：业务区锁定、凭据清空
4. 通过条件：以上步骤可重复执行且无崩溃；动作提示与数据来源标识（Mock 演示）明确不冒充真实服务端

## 3. Web 大屏冒烟路径

```bash
python dashboard/serve.py --port 61469        # Windows；Ubuntu 用 python3
# 浏览器访问 http://127.0.0.1:61469
# 离线答辩演练开关：?map=topology
```
1. 主屏加载：7d/30d 营收趋势、桩状态分布、站点利用率；更新时间与数据来源说明可见
2. `?map=topology` 离线拓扑可用；无腾讯 Key 时自动拓扑降级
3. 数据真实性：指标可追溯到 `dashboard/data/demo.json`（金额分、UTC、demo 标识）

## 4. 联调（9/7 闸门后追加，随 Socket 接入）

- 见 `docs/meetings/interface-gate-2026-09-07.md` 结论与检查清单
- 启动：`EV_ADMIN_DATA_SOURCE=socket ./admin-client`（需 B 服务端运行，默认 127.0.0.1:45454）

## 5. 执行记录（执行时填写）

| 日期 | 平台 | 结果 | 证据（截图/输出文件） |
|---|---|---|---|
| （9/8 计划） | Windows |  |  |
| （9/8 计划） | Ubuntu VM |  |  |
