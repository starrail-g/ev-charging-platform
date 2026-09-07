# 角色 C 回归结果记录（C-S1-022）

> 计划执行日：9/9（内部冻结前全量回归；9/8 发布候选前跑一轮预告）。
> 范围：管理端 Qt 三套测试 + Web node 测试 + 双平台（Windows 开发机 / Ubuntu VM）。
> 更新规则：执行后记录**真实命令输出**（Totals 行逐字），不把"未运行"写成"通过"。

## 1. 回归范围与命令

### 1.1 Qt 管理端（三套 exe，offscreen）

Windows（构建目录 `D:/work/chargingplatform/build/admin-client-ui`）：
```bash
export PATH="/c/Python314:/d/Qt/6.2.4/mingw_64/bin:/d/Qt/Tools/mingw1310_64/bin:/c/Windows/System32:/c/Windows"
export QT_QPA_PLATFORM=offscreen
./tests/ui/release/tst_ui.exe -o ui-result.txt,txt
./tests/release/tst_launchsmoke.exe -o smoke-result.txt,txt
./tests/loginflow/release/tst_loginflow.exe -o loginflow-result.txt,txt
```
Ubuntu（构建目录仓库外，exe 路径以 find 结果为准但必须实跑）：
```bash
QT_QPA_PLATFORM=offscreen ./tests/ui/tst_ui -o ui-result.txt,txt
QT_QPA_PLATFORM=offscreen ./tests/tst_launchsmoke -o smoke-result.txt,txt
QT_QPA_PLATFORM=offscreen ./tests/loginflow/tst_loginflow -o loginflow-result.txt,txt
```

### 1.2 Web 大屏（node + serve 门禁）

```bash
node --test dashboard/tests/*.test.mjs     # 必须 glob 文件，不能只传目录（node22 会把目录当单测试）
python dashboard/serve.py --check          # Ubuntu: python3
python scripts/generate_ui_tokens.py --check
```

## 2. 历史基线（9/4，PR #8 合入前）

| 套件 | 基线 |
|---|---|
| tst_ui | 20 passed（PR #8）；管理操作批后 24 passed（9/4 本地） |
| tst_launchsmoke | 6 passed |
| tst_loginflow | 7 passed |
| node | 35 passed |
| serve --check / tokens --check | 通过 |

## 3. 执行记录（执行时填写；Windows 与 VM 各一行）

| 日期 | 平台 | tst_ui | launchsmoke | loginflow | node | --check | 结论 |
|---|---|---|---|---|---|---|---|
| 9/4（预跑） | Windows | 24 | 6 | 7 | 待填 | 待填 |  |
| （9/9 计划） | Windows |  |  |  |  |  |  |
| （9/9 计划） | Ubuntu VM |  |  |  |  |  |  |

## 4. 失败处理约定

- 任何失败先记缺陷日志（docs/release/defect-log.md，C-S1-xxx），修复后重跑受影响套件 + 全量；
- 9/9 18:00 内部冻结前最后一次全量必须双平台同绿，截图/输出文件随包提交。
