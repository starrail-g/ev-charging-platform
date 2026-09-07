# admin-client —— PC 管理端（Qt）

Linux + Qt Widgets 的充电桩管理应用：管理员登录、概览、桩/站点/用户管理。

## 构建系统（已决策）

- **2026-09-01 与 A/B 确认：使用 qmake（`.pro`）**，不维护 CMake。
- 理由：Ubuntu 验收环境 qmake6 现成；东软 Qt 教程以 `.pro` 为主线。
- 决策记录见 `docs/requirements/README.md` §3（构建系统二选一，已确认）与 `current.md`。

## 环境要求

### 开发环境（Windows 本机）

| 组件 | 版本 | 路径 |
|---|---|---|
| Qt | 6.2.4 (MinGW 64-bit) | `D:/Qt/6.2.4/mingw_64` |
| 编译器 | MinGW 13.1.0 64-bit | `D:/Qt/Tools/mingw1310_64/bin` |
| 构建工具 | mingw32-make（随编译器） | 同上 |
| 其他 | git 2.53.0；python3 3.14.3 | 系统 PATH |

⚠️ 本机 PATH 默认 `g++` 为 MinGW.org 6.3.0（过老），构建前必须将 Qt 工具链置于 PATH 前面：

```bash
export PATH="/d/Qt/6.2.4/mingw_64/bin:/d/Qt/Tools/mingw1310_64/bin:$PATH"
```

### 验收环境（Ubuntu 22.04 虚拟机）

- Qt 6（qmake6）、g++、make、git、python3；版本记录见 9/7 干净环境验证（本文件届时补充）。

## 工作区路径

- 项目仓库：仓库根目录（下文以 `<repo>` 指代）
- 仓库外构建目录：`<repo>` 同级 `build/admin-client`（如 `../build/admin-client`）
- 构建产物不得生成在项目仓库内部。

## 构建与测试（qmake 唯一路径）

```bash
# 在仓库根目录执行；构建目录位于仓库外层
mkdir -p ../build/admin-client && cd ../build/admin-client
qmake ../../ev-charging-platform/apps/admin-client/admin-client.pro  # Ubuntu 用 qmake6
make -j
# 标准验收命令（Ubuntu/CI/无图形会话统一离屏运行，避免 xcb 连接失败）：
QT_QPA_PLATFORM=offscreen make check
# 有桌面会话时可省略环境变量：
# make check
```

Windows 下 `make` 为 `mingw32-make`（需先 export PATH，见上）。
⚠️ git-bash 下 `mingw32-make check` 会报 syntax error（MSYS sh 兼容问题），
验证测试直接运行测试可执行文件：

```bash
./tests/release/tst_launchsmoke.exe -o result.txt,txt   # 退出码 0 且 Totals 全 PASS 即通过
```

## 运行

```bash
# 构建完成后
./src/admin-client
# Windows: .\src\release\admin-client.exe
```

## Mock / Socket 数据源切换

数据源在 `src/main.cpp` 工厂点按环境变量收敛，页面层一律经 `AdminRepository` 抽象、不感知数据源。

默认（未设置 `EV_ADMIN_DATA_SOURCE`，或值非 `socket`）使用 `MockAdminRepository`：
固定演示数据，桩重启/冻结动作为模拟执行（成功提示带"（模拟）"后缀），无需服务端。

连接真实服务端时（Socket 模式）：

```bash
EV_ADMIN_DATA_SOURCE=socket ./admin-client            # git-bash / Ubuntu
# Windows cmd: set EV_ADMIN_DATA_SOURCE=socket && admin-client.exe
```

- 仅当 `EV_ADMIN_DATA_SOURCE` 恰为 `socket` 时才启用 `SocketAdminRepository`；空值与其
  它值均为 Mock。
- 服务端默认地址 `127.0.0.1:45454`，可用 `EV_SERVER_HOST` / `EV_SERVER_PORT` 覆盖
  （键名与 `config/example.env` 一致）。
- 阶段一口径：9/7 18:00 接口闸门前默认保持 Mock；socket 模式以真实服务端联调验收，
  现场记录见 `docs/meetings/interface-gate-2026-09-07.md`，演示材料不冒充真实联调。

## 已知限制

- 第一阶段"桩重启""冻结/解冻"为模拟操作，非真实硬件控制。
- 页面未实现服务端逻辑前，业务数据均为 Mock。
