# Development seed data

`dev.sql` contains deterministic stations, piles, users, an administrator and
completed orders for local demonstrations and dashboard queries. It is safe to
run repeatedly because inserts use stable primary keys and `INSERT OR IGNORE`.

The administrator login is `admin` / `123456`. The password is stored as the
SHA-256 digest documented in the database architecture note; clients must not
store or transmit this seed password outside local development.

```sh
sqlite3 var/ev-charging.db < database/seeds/dev.sql
```
