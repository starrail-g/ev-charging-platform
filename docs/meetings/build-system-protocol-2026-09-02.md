# 项目构建系统强制协议

**生效日期**：2026-09-02
**适用范围**：整个 `ev-charging-platform` 仓库及所有成员分支、Pull Request、答辩构建和交付材料

## 1. 强制决定

项目 Qt/C++ 模块统一使用 `qmake6` 构建和测试。`CMake`、`cmake`、CMake preset、CMake 构建目录和 CMake 构建命令不得作为本项目的构建、测试、验收或发布依据。

该决定覆盖此前“CMake 或 qmake 尚未确定”的旧记录。发生冲突时，以本协议和最新 `current.md` 为准。

## 2. 执行要求

- Qt 工程必须提供可由 `qmake6` 读取的 `.pro` 文件；多模块工程使用 `TEMPLATE = subdirs` 或明确的 `.pro` 依赖组织。
- 构建前必须在 Ubuntu 环境检查 `qmake6 --version`，并记录 Qt 版本、编译器版本和实际命令。
- 单元测试、协议测试、服务端测试和用户端测试均必须通过对应 `.pro` 文件生成 Makefile 后执行；不得用 CMake 替代。
- PR 描述中的构建、测试、启动和覆盖率证据必须来自 `qmake6` 路径；仅有 CMake 结果不能标记任务完成。
- 干净环境验证应从新构建目录开始，例如 `build/qmake6/<module>`；构建目录不得提交到 Git。
- `current.md`、模块 README、任务报告和 PR 描述必须统一记录 qmake6 命令及其结果。

## 3. 现有文件处理

仓库中已有的 `CMakeLists.txt` 和 `apps/user-client/CMakeLists.txt` 属于历史尝试，不是当前构建入口。在补充 qmake6 工程前，不得继续执行或引用这些文件；不得把旧 CMake 验证结果作为当前验收证据。后续清理或删除这些文件应作为独立提交处理。

## 4. 例外与责任

本协议不允许以“本机没有 qmake6”为理由改用 CMake；应记录环境阻塞，并在 Ubuntu/VM 中完成 qmake6 验证。B 负责服务端、协议和数据库工程的 qmake6 入口，A 负责用户端 qmake6 入口及适配，C 负责管理端、大屏和跨模块 qmake6 构建/发布验证。任何构建系统变更必须同步 `current.md`。

## 5. 最低证据

每个模块至少保留以下脱敏记录：

```text
qmake6 --version
qmake6 <module>.pro
make -j<parallelism>
./<test-or-application>
```

命令必须在模块 README 或任务报告中说明，输出中不得包含密钥、Token、密码或用户隐私。
