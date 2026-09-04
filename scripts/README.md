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

在 Linux/Ubuntu 虚拟机中配置本地环境变量后执行：

```bash
read -s TENCENT_MAP_KEY
export TENCENT_MAP_KEY
bash scripts/tencent_poi_probe.sh
```

脚本调用腾讯位置服务周边搜索接口，使用固定深圳坐标和“充电站”关键词，输出 HTTP/腾讯状态、结果数量和第一条 POI 摘要。Key 只从环境变量传入，不会打印、写入文件或记录到 Git。未配置 Key 时脚本明确失败并退出。
