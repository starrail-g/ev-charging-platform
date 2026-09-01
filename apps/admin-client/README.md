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

- 项目仓库：`D:/work/chargingplatform/ev-charging-platform`
- 仓库外构建目录：`D:/work/chargingplatform/build/admin-client`
- 构建产物不得生成在项目仓库内部。

## 构建与测试（qmake 唯一路径）

```bash
# 在仓库根目录执行；构建目录位于仓库外层
mkdir -p ../build/admin-client && cd ../build/admin-client
qmake ../../ev-charging-platform/apps/admin-client/admin-client.pro  # Ubuntu 用 qmake6
make -j
make check        # 运行 Qt Test（Ubuntu 验收环境）
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

## Mock / Socket 切换

- 第一阶段默认 `MockAdminRepository`（固定演示数据）。
- 9/7 18:00 接口闸门通过后切换 `SocketAdminRepository`；未通过则保持 Mock 并标注。
- 切换方式：TODO（随 9/6 适配层实现补充）。

## 已知限制

- 第一阶段"桩重启""冻结/解冻"为模拟操作，非真实硬件控制。
- 页面未实现服务端逻辑前，业务数据均为 Mock。
