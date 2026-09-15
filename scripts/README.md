# Project scripts

## Database migration

Apply a versioned SQLite migration with fail-fast, atomic execution:

```sh
python3 scripts/migrate_db.py var/ev-charging.db \
  database/migrations/001_v0.1_to_v0.2.sql

# Use this instead when the database is already at v0.2.
python3 scripts/migrate_db.py var/ev-charging.db \
  database/migrations/002_v0.2_to_v0.3.sql
```

The runner commits only after the migration exits without an error, confirms
the expected schema version, and passes `PRAGMA foreign_key_check`. Any error
causes rollback and a non-zero exit status.
## 腾讯地图 POI 探测

在 B 服务端所在的 Linux/Ubuntu 虚拟机中配置本地环境变量后执行；用户端不运行此脚本，也不持有腾讯 Key：

```bash
read -s TENCENT_MAP_KEY
export TENCENT_MAP_KEY
export TENCENT_MAP_ENABLED=1
bash scripts/tencent_poi_probe.sh
```

脚本调用腾讯位置服务周边搜索接口，使用固定深圳坐标、当前官方允许的 1000 米半径和“充电站”关键词。输出仅包含接口名称、HTTP/腾讯状态、结果数量、字段完整性和是否降级；不输出完整 URL、原始 JSON、POI 明细或 Key。Key 只从环境变量传入，不会打印、写入文件或记录到 Git。未配置 Key 或显式禁用地图时脚本明确失败并退出。

## 二阶段一键启动

先在被 `.gitignore` 忽略的 `config/local.env` 中填写运行时配置（三个腾讯 Key
可以使用同一个值），然后执行：

```bash
./scripts/start_stage2.sh
```

工作台数据或分析代码更新后，脚本会通过 `/tmp/ev-s2-present/.stage2-data-version` 自动识别版本并重建数据库、ODS、Spark 分层、模型与 ADS。也可以显式强制刷新：

```bash
EV_S2_REFRESH=1 ./scripts/start_stage2.sh
```

脚本会按需准备国内镜像依赖、生成 Schema v0.4 分析数据、运行 Spark 分层与
模型、构建缺失的 Qt 程序，并启动真实 Socket 服务端、Flask 分析 API、Dashboard
和桌面环境下的用户端/管理端。数据与日志位于 `/tmp/ev-s2-present`、
`/tmp/ev-s2-final-build`、`/tmp/ev-s2-run`，按 `Ctrl-C` 可统一停止。

无图形桌面时只启动三个后台服务：

```bash
EV_S2_START_GUI=0 ./scripts/start_stage2.sh
```

可用 `EV_S2_OUT`、`EV_S2_BUILD` 和 `EV_S2_RUN_DIR` 覆盖默认的仓库外目录。
