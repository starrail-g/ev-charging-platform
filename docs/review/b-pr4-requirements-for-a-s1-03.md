# B PR#4 完成要求：解锁 A-S1-03

> 适用范围：B 同学 PR#4 的数据库、服务端和 Protocol v1 实现
> 目标：B 完成本清单后，A 可以结束 A-S1-03；后续只保留 A-S1-04 的联调、回归和交付验证。
> 基准提交：7ad915302c28136d8b0e9a8c34363266412c58eb（已完成，本文作为评审记录保留）
> 协议依据：docs/architecture/protocol.md、docs/architecture/database.md、docs/meetings/protocol-summary-2026-09-02.md、database/schema/schema.sql
> 2026-09-04 复核：PR#4 head 已补齐 user.profile.*、wallet.recharge、1101 冻结策略、stop 释放桩、结算扣款/回滚、幂等和并发测试；A-S1-03 解锁条件已满足。

## 1. 完成边界

B PR#4 必须提供可从空数据库启动的、事务化的真实服务端闭环。A 端不得再通过临时字段、Mock 逻辑或数据库直读补齐缺口。

B 完成后，A-S1-03 应达到以下状态：

- A 的 SocketUserService 可以只依据冻结协议调用所有用户端业务接口。
- 手机号免密登录、自动建号、资料、余额、站点、电桩、预约、取消、开始、停止、结算、当前订单和历史订单全部可用。
- A 端收到的状态、金额、时间、错误码和资源 ID 与数据库一致。
- A 不需要修改 UI 或 IUserService 接口来适配 B 的实现。
- A-S1-04 只需执行真实联调、异常回归、干净环境构建和交付记录。

以下任一项未完成，A-S1-03 不能标记完成：

- profile 或 recharge 仍返回 unsupported；
- 结算未扣余额、未写 charge 钱包流水或非原子；
- 预约、桩状态和订单状态可能半更新；
- 错误码、字段名、状态名或金额类型未冻结；
- 空数据库无法初始化，或重复启动会破坏数据；
- 没有可重复的启动命令、种子数据和协议样例；
- 并发、重复请求、超时和数据库失败没有稳定结果。

## 2. 必须实现的用户端接口

所有请求和响应都必须遵循 Protocol v1 的四字节大端长度帧、UTF-8 JSON envelope、v=1、id、type、payload 规则。成功响应类型为 request type 加 .result，失败统一使用 type=error。

### 2.1 user.login

请求：

~~~json
{"v":1,"id":"a-uuid","type":"user.login","payload":{"phone":"13800000000"}}
~~~

要求：

- 只接受 11 位数字手机号。
- 已存在且 status=active：直接返回用户。
- 不存在：在同一事务内自动创建用户，昵称为 用户加手机号后四位，余额为 0，status=active，然后返回用户。
- status=frozen 的用户登录成功并返回 status=frozen；预约创建/确认、开始充电和充值返回 1101 ACCOUNT_FROZEN，资料/订单读取及取消、停止、结算放行。
- 不能要求密码；A 的登录页面传入的兼容密码字段必须被忽略。
- 响应 user 至少包含 id、phone、nickname、balance_cents、status；avatar_path 可为空。
- 同一手机号并发首次登录只能创建一条用户记录，不能返回唯一约束或数据库错误。

### 2.2 user.profile.get

请求 payload：user_id（JSON integer）。

要求：

- 校验 user_id 存在且用户处于 active。
- 返回完整 user DTO。
- 不得返回密码、密码哈希、数据库路径或内部异常信息。
- 用户不存在返回 1200；用户冻结返回 1100。

### 2.3 user.profile.update

请求 payload：user_id，以及可选 nickname、avatar_path。

要求：

- 至少提供一个可修改字段；nickname 去除首尾空白后不能为空，并限制合理长度。
- avatar_path 只作为客户端本地路径或已约定的安全标识保存，不得读取任意文件或回显敏感路径。
- 更新必须写入 users.updated_at，并在响应中返回最新 user。
- 资料更新成功后，重新登录或再次 profile.get 必须保持修改结果。
- 并发更新不得覆盖未修改字段；数据库失败必须完整回滚。

### 2.4 wallet.recharge

请求 payload：user_id（JSON integer）、amount_cents（正整数）。

要求：

- 金额只能使用整数分，禁止浮点元。
- amount_cents 必须大于 0，并设置合理上限，超限返回 1002。
- 在同一事务内更新 users.balance_cents，并插入 wallet_transactions：
  - transaction_type=recharge；
  - amount_cents 为正数；
  - balance_after_cents 为更新后的余额；
  - idempotency_key 必须可追踪。
- 返回 balance_cents 和 transaction_id。
- 用户不存在、冻结、参数错误和数据库失败分别返回稳定错误。
- 重复请求 ID 或相同幂等键不得重复充值。

### 2.5 station.list

要求：

- 支持空查询和关键词查询；查询失败、超时和空结果都要有合法响应。
- 返回 stations 数组，字段至少包括：
  id、name、address、latitude、longitude、status/open、pile_total、pile_idle、pile_reserved、pile_charging、price_cents_per_kwh（如站点统一价格可提供）。
- pile_idle 必须实时统计 charging_piles.status=idle 的数量，不能使用缓存的手填数字。
- latitude 范围为 [-90,90]，longitude 范围为 [-180,180]。
- 未提供定位参数时也必须返回确定性的排序结果；若支持 distance_km，必须说明计算口径和单位。
- 站点停用时必须以明确状态返回，不能伪装成营业。

### 2.6 pile.list

请求 payload：station_id（JSON integer）。

要求：

- 返回该站全部充电桩，字段至少包括：
  id、station_id、pile_code、pile_type、power_kw、unit_price_cents_per_kwh、status。
- status 只能使用 idle、reserved、charging、fault、offline。
- 不得返回维护、不可用等未冻结的额外状态名。
- 不存在站点返回 1200；空桩列表返回合法空数组。
- 列表状态必须来自数据库当前值，不能由客户端推断。

### 2.7 order.active.get

要求：

- 请求 user_id 必须验证存在且 active。
- 有未完成订单时返回 order；没有时 payload.order 必须为 JSON null，不能伪造空对象。
- 未完成订单范围固定为 pending_reservation、reserved、charging、pending_settlement。
- 返回 order 至少包括：
  id、order_no、user_id、pile_id、status、unit_price_cents_per_kwh、service_fee_cents、total_amount_cents、created_at、reserved_at、started_at、ended_at、settled_at；可附 station_name、station_address、pile_code。
- 同一用户最多一条活动订单。

### 2.8 order.history.list

要求：

- 只返回该用户已完成或已取消的历史订单，除非文档另有明确约定。
- 默认按完成/结算时间倒序，最新一条在前。
- 金额全部为整数分；时间为 UTC ISO-8601。
- 不得泄露其他用户订单。
- 空历史返回 orders=[]。

### 2.9 reservation.create

请求 payload：user_id、pile_id。

要求：

- 校验用户 active、桩存在且为 idle、用户没有活动订单。
- 一个事务内插入 pending_reservation 订单，并将桩从 idle 改为 reserved。
- 返回 order.status=pending_reservation 和 pile.status=reserved。
- 价格从桩的 unit_price_cents_per_kwh 固化到订单，不能依赖客户端传价。
- 用户重复预约、桩被占用、桩 fault/offline、用户冻结分别返回 1201 或 1100 等冻结错误码。
- 唯一索引和服务层检查必须同时存在，避免竞态窗口。

### 2.10 reservation.confirm

要求：

- 只允许 pending_reservation → reserved。
- 必须校验 user_id 是订单所属用户。
- 成功响应返回完整 order，status=reserved。
- 重复 confirm 返回可预测的 1201，不能创建第二个订单。

### 2.11 reservation.cancel

要求：

- 只允许 pending_reservation 或 reserved → cancelled。
- 同一事务更新订单和桩：订单 cancelled、桩 idle。
- 取消后用户可以重新预约；不得写入充电扣款流水。
- 重复取消返回幂等成功或稳定 1201，行为必须写入文档并测试。

### 2.12 charging.start

A 端主链路使用 reservation order_id 调用；B 可保留 pile_id 直启，但必须明确两条路径的规则。

要求：

- 预约启动只允许 reserved → charging，并将桩 reserved → charging。
- 同一事务更新订单和桩。
- 用户已有其他活动订单、桩状态冲突、订单不属于用户分别返回稳定错误。
- started_at 必须由服务端生成 UTC 时间。
- 重复 start 不得创建新订单或重复变更桩状态。

### 2.13 charging.stop

要求：

- 只允许 charging → pending_settlement。
- 服务端生成 ended_at；客户端传入的 ended_at 只能作为受控测试字段，不能信任任意未来时间。
- 根据 energy_wh、unit_price_cents_per_kwh、service_fee_cents 计算 total_amount_cents，使用整数算术。
- 同一事务更新订单 charging_pile：订单进入 pending_settlement，桩回到 idle。
- 返回 order 和 estimated_amount_cents 或 total_amount_cents。
- 重复 stop 不得重复计费。

### 2.14 charging.settle

要求：

- 只允许 pending_settlement → completed。
- 在一个数据库事务中完成：
  1. 读取并锁定用户余额；
  2. 校验余额大于等于 total_amount_cents；
  3. 扣减 users.balance_cents；
  4. 插入一条 transaction_type=charge 的负数 wallet_transactions；
  5. 设置 balance_after_cents；
  6. 设置订单 settled_at，并更新为 completed。
- 余额不足必须返回 1202，并保证订单、余额、钱包流水全部不变。
- 成功后必须满足 schema 对 completed order 的 charge 流水触发器约束。
- 每个订单最多一条 charge 流水，重复 settle 必须幂等或稳定冲突，不能重复扣款。
- 返回完整 order 和最新 balance_cents。
- 结算失败、数据库异常或进程中断必须回滚全部写入。

## 3. 数据库完成要求

### 3.1 Schema 和迁移

- schema.sql 可在空 SQLite 文件一次执行成功。
- 迁移脚本可重复执行，重复执行不会丢数据或重复插入。
- 每条数据库连接执行 PRAGMA foreign_keys=ON。
- 所有 ID 使用 INTEGER；金额使用 INTEGER cents；时间使用 UTC ISO-8601。
- users.phone 唯一且满足 11 位数字约束。
- charging_piles 的 station_id、pile_code、status、价格和功率约束有效。
- charging_orders 的用户/桩外键、状态约束、金额非负约束有效。
- 活动用户订单和活动桩订单的唯一索引必须保留。
- wallet_transactions 必须保留 recharge、charge、refund、adjustment 类型约束和订单扣款唯一索引。
- 完成订单必须有匹配的 charge 流水；禁止删除或篡改已完成订单的扣款流水。
- 所有写操作必须使用参数绑定，禁止拼接用户输入 SQL。

### 3.2 事务和并发

至少覆盖以下竞态：

- 两个用户同时预约同一 idle 桩：只能一个成功。
- 同一用户同时预约两个桩：只能一个成功。
- 两个客户端同时 start：只能一个成功。
- 两个客户端同时 settle：只能扣款一次。
- 余额不足结算：订单和余额保持原值。
- 数据库写失败：订单、桩、余额和钱包流水全部回滚。
- 服务端重启后，活动订单和桩状态从数据库恢复，不依赖内存缓存。

## 4. Protocol v1 完成要求

- 文档、代码和样例中的 operation type、字段名、状态名完全一致。
- request id 长度 1–64，服务端返回同一个 id。
- 服务端可正确处理半包、粘包、单次读取多个 frame 和 malformed frame。
- 单个 payload 大小限制为 1 MiB；无效长度和无效 JSON 返回 1000/1001。
- 不支持的版本返回 1003；不支持的操作返回 1002。
- 业务错误只使用冻结错误码：
  1100 未授权、1200 不存在、1201 冲突、1202 余额不足、1300 数据库错误、1500 内部错误。
- 客户端可根据 code 分支，不能依赖 message 文本。
- state-changing 请求必须支持 request id 或业务幂等键的重复检测；同一 id 重放应返回同一业务结果，不能再次写入。
- 不同操作复用同一 request id 必须返回明确冲突，不得执行第二个操作。
- 断连、超时和服务端关闭不能留下半事务。
- 错误响应不得包含 SQL、绝对数据库路径、堆栈、密钥或密码哈希。

## 5. 种子数据和启动交付

B 必须在 PR 中提供：

- 空目录初始化命令。
- 数据库迁移和 seed 命令。
- 服务端启动命令、默认端口和环境变量示例。
- 至少一个 active 演示用户，余额足以完成一次结算。
- 至少一个 frozen 用户。
- 至少两个站点，覆盖 idle、reserved/charging、fault、offline 桩状态。
- 至少一条可查询的历史完成订单和钱包 charge 流水。
- 重置演示数据的方法；不得把运行数据库、日志和构建产物提交到 Git。
- API Reference 中每个用户端 operation 的 request/response/error JSON 样例。

## 6. 必须提交的后端测试和证据

PR#4 合并前至少提供以下自动化测试：

- 空数据库 schema 和 migration smoke。
- 手机号登录成功、自动注册、重复登录、冻结用户。
- profile get/update 持久化。
- recharge 正常、非法金额、重复幂等、冻结用户。
- station.list / pile.list 正常、空结果、非法 ID。
- reservation create/confirm/cancel 状态转换。
- 同用户重复预约、同桩竞争、故障桩和 offline 桩。
- charging start/stop 的状态和桩同步更新。
- settle 成功扣款、余额不足回滚、重复 settle 不重复扣款。
- order.active.get 的 null 语义和 order.history.list 倒序。
- 半包/粘包、多 frame、错误版本、错误 JSON、重复 request id。
- 客户端断连、服务端重启和数据库异常回滚。
- 最少一条从 user.login 到 order.history.list 的真实端到端脚本。

建议输出：

~~~text
PASS: schema/migration
PASS: protocol framing
PASS: user/order/wallet handlers
PASS: concurrency and rollback
PASS: end-to-end user lifecycle
Output directory: <clean temporary directory>
~~~

## 7. A 端解锁条件

B 提交完成后，A 只需：

1. 获取 B 的最终 commit 和协议样例。
2. 确认 A SocketUserService 无需改动字段名、状态名和金额类型。
3. 在干净 Ubuntu 目录使用 qmake6 构建用户端和 QtTest。
4. 启动 B 服务端和全新数据库。
5. 执行登录、站点、电桩、预约、开始、停止、结算、历史订单。
6. 核对数据库可观察结果：订单状态、桩状态、余额、钱包流水和时间。
7. 执行异常和断连回归。
8. 更新 A-S1-04 测试记录和 current.md。

如果第 2 步仍需要修改 A 的 IUserService、页面字段或临时映射，说明 B 的 PR 尚未达到 A-S1-03 解锁标准。

## 8. B 交付前自检清单

- [ ] 所有用户端 operation 已实现并有样例
- [ ] profile/recharge 不再返回 unsupported
- [ ] user.login 为手机号免密并自动建号
- [ ] 所有金额为整数分
- [ ] settle 原子扣款并写 charge 流水
- [ ] 余额不足完整回滚
- [ ] 活动用户/桩唯一性在并发下成立
- [ ] reservation.create 返回 pending_reservation
- [ ] reservation.confirm/cancel 状态和桩状态正确
- [ ] charging.start/stop 状态和桩状态同步
- [ ] active order 无数据返回 null
- [ ] history 按完成时间倒序
- [ ] 错误码、状态、字段与 protocol.md 一致
- [ ] 重复 request id 和幂等行为有测试
- [ ] 半包/粘包和断连有测试
- [ ] 空数据库、迁移、seed、启动命令可复现
- [ ] 无密钥、密码、数据库、日志和构建产物进入 PR
- [ ] qmake6 构建和后端 smoke 结果为 PASS

## 9. 依赖关系

~~~mermaid
flowchart TD
    BDB[B PR#4 数据库/事务/种子完成] --> BAPI[用户端 Protocol v1 handlers 完成]
    BAPI --> A103[A-S1-03 Socket 适配与真实闭环完成]
    A103 --> A104[A-S1-04 联调/回归/交付]
~~~
