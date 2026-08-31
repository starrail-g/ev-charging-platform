项目协作指南

本文档规定团队使用 Git 和 GitHub 进行代码协作的基本流程。

---
1. 最重要的规则
不要直接在 `main` 分支开发。
不要直接向 `main` 分支 push。
每个任务使用独立分支。
完成任务后通过 Pull Request（PR）合并。
合并前至少进行一次代码检查。
不要修改与自己任务无关的代码。
不要提交密码、API Key、Token、私钥等敏感信息。
不要使用 `git push --force`。
---
2. 我们的协作流程
日常开发只需要记住：
``` text
领取任务
   ↓
更新 main
   ↓
创建自己的分支
   ↓
开发和测试
   ↓
commit
   ↓
push 到 GitHub
   ↓
创建 Pull Request
   ↓
代码 Review
   ↓
合并到 main
```
所有正式代码最终都通过 Pull Request 进入 `main`。
---
3. 分支命名
统一使用：
``` text
类型/简短英文描述
```
常用类型：
类型          用途       示例
---
`feature/`    新功能     `feature/user-login`
`fix/`        Bug 修复   `fix/login-crash`
`docs/`       文档       `docs/socket-protocol`
`test/`       测试       `test/database`
`refactor/`   重构       `refactor/database-layer`
`chore/`      工程配置   `chore/build-script`
一个分支尽量只解决一个明确的问题。
---
4. 提交代码
完成一个阶段后，先查看：
``` bash
git status
```
然后添加需要提交的文件：
``` bash
git add 文件名
```
例如：
``` bash
git add src/login.cpp
```
如果确认当前所有修改都属于这个任务，也可以：
``` bash
git add .
```
再次检查：
``` bash
git status
```
然后提交：
``` bash
git commit -m "feat: implement user login"
```
---
5. 项目文档
项目中的重要文档包括：
``` text
README.md
    项目介绍和启动入口

CONTRIBUTING.md
    Git / GitHub 团队协作方法

AGENTS.md
    AI/Codex 项目开发规则

current.md
    当前项目状态、TODO、架构和近期进展

docs/
    需求、架构、数据库、通信协议、UI 等正式文档
```
涉及公共接口、Socket 协议、数据库结构或架构的重要修改， 不要只修改代码。
对应文档也应该一起更新。
---
6. AI / Codex 使用规则
AI 生成的代码仍然由提交者负责
提交前必须理解重要代码
进行必要测试
不要让 AI 擅自 push 或合并代码
不要把项目密钥提供给 AI
