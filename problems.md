# Problems

本文档整理课程平台中的 DBMS 实践题目。编号 `P01-P10` 仅用于本仓库内部管理，未必与平台官方题号完全一致。

## 题目索引

| 编号 | 题目 | 前置要求 | 状态 |
| --- | --- | --- | --- |
| P01 | 存储管理 | 无 | 已完成 |
| P02 | 查询执行 | P01 | 已完成 |
| P03 | BIGINT 类型 | P02 | 已完成 |
| P04 | DATETIME 类型 | P02 | 已完成 |
| P05 | 唯一索引 | P02 | 已完成 |
| P06 | 聚合函数 | P02 | 已完成 |
| P07 | ORDER BY 与 LIMIT | P02 | 已完成 |
| P08 | 事务控制语句 | 查询执行、唯一索引、DATETIME | 已完成 |
| P09 | 可串行化与死锁预防 | P08、唯一索引 | 已完成 |
| P10 | WAL 与故障恢复 | P09 | 已完成 |

## 通用要求

- 除 P01 明确限制接口外，其余功能题允许修改、增加或删除数据结构和接口，也允许重构框架。
- 非法 SQL 或非法数据应输出 `failure`。
- 不应向客户端或 `output.txt` 输出无关调试信息。
- 单线程 SQL 测试通常检查数据库目录下的 `output.txt`。
- 多线程测试通常检查客户端收到的返回字符串。
- 输出列顺序和格式必须符合测试要求。
- 完成题目后，应在对应章节记录实现文件、测试命令和验证结果。

---

## P01 - 存储管理

状态: 已完成

类型: 内核代码填充

知识点: 文件存储、页面管理、记录组织、缓冲池、LRU

### 约束

- 测试代码会直接调用指定接口。
- 不得修改已有接口。
- 不得删除已有数据结构或成员变量。
- 可以增加辅助接口、数据结构和成员变量。
- `src/unit_test.cpp` 只提供参考测试，最终测试不限于其中的用例。

### 1. 磁盘管理器

相关文件:

- `src/errors.h`
- `src/common/config.h`
- `src/storage/disk_manager.h`
- `src/storage/disk_manager.cpp`

需要实现:

```cpp
void DiskManager::create_file(const std::string &path);
int DiskManager::open_file(const std::string &path);
void DiskManager::close_file(const std::string &path);
void DiskManager::destroy_file(const std::string &path);
void DiskManager::write_page(
    int fd, page_id_t page_no, const char *offset, int num_bytes);
void DiskManager::read_page(
    int fd, page_id_t page_no, char *offset, int num_bytes);
```

功能要求:

- 创建、打开、关闭和删除指定文件。
- 从指定页面起始位置读取 `num_bytes` 字节。
- 从指定页面起始位置写入 `num_bytes` 字节。
- 文件和系统调用错误应使用框架已有异常处理。

### 2. 缓冲池与 LRU

相关目录:

- `src/storage/`
- `src/replacer/`

需要实现的 LRU 接口:

```cpp
bool LRUReplacer::victim(frame_id_t *frame_id);
void LRUReplacer::pin(frame_id_t frame_id);
void LRUReplacer::unpin(frame_id_t frame_id);
```

功能要求:

- `victim` 淘汰最近最少使用且未被固定的帧。
- `pin` 将帧标记为不可淘汰。
- `unpin` 将帧加入可淘汰集合。
- 重复 `pin/unpin` 不应破坏内部状态或计数。

需要实现的缓冲池接口:

```cpp
Page *BufferPoolManager::new_page(PageId *page_id);
Page *BufferPoolManager::fetch_page(PageId page_id);
bool BufferPoolManager::find_victim_page(frame_id_t *frame_id);
void BufferPoolManager::update_page(
    Page *page, PageId new_page_id, frame_id_t new_frame_id);
bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty);
bool BufferPoolManager::delete_page(PageId page_id);
bool BufferPoolManager::flush_page(PageId page_id);
void BufferPoolManager::flush_all_pages(int fd);
```

功能要求:

- 优先使用空闲帧，没有空闲帧时调用 LRU 选择 victim。
- 页面命中后增加 pin count。
- 页面首次读入时建立 page table 映射。
- 淘汰脏页前必须刷盘。
- pin count 降至 0 后，页面才能进入 replacer。
- 正在使用的页面不能删除。
- `flush_page` 无论页面是否为脏页都应写入磁盘。
- `flush_all_pages` 只刷新指定文件对应的页面。
- 缓冲池操作需要考虑并发安全。

### 3. 记录管理器

相关目录: `src/record/`

需要实现的记录接口:

```cpp
std::unique_ptr<RmRecord> RmFileHandle::get_record(
    const Rid &rid, Context *context) const;
Rid RmFileHandle::insert_record(char *buf, Context *context);
void RmFileHandle::insert_record(const Rid &rid, char *buf);
void RmFileHandle::delete_record(const Rid &rid, Context *context);
void RmFileHandle::update_record(
    const Rid &rid, char *buf, Context *context);
```

功能要求:

- 使用 `Rid` 唯一定位记录。
- 自动选择空闲槽位插入记录。
- 插入或删除后同步更新文件头、页面头和空闲页链表。
- 指定 `Rid` 的插入接口用于事务回滚与故障恢复。
- 正确管理页面 pin、dirty 和 unpin 状态。

需要实现的扫描接口:

```cpp
RmScan::RmScan(const RmFileHandle *file_handle);
void RmScan::next();
bool RmScan::is_end() const;
```

功能要求:

- 初始化到第一条有效记录。
- 跳过空槽和无有效记录的页面。
- 正确判断扫描结束。

### 验证

- [x] DiskManager 文件生命周期测试通过。
- [x] DiskManager 页面读写测试通过。
- [x] LRUReplacer 测试通过。
- [x] BufferPoolManager 测试通过。
- [x] RmFileHandle 测试通过。
- [x] RmScan 测试通过。

### 实现记录

完成日期: 2026-06-25

主要修改:

- `src/storage/disk_manager.cpp`
- `src/replacer/lru_replacer.cpp`
- `src/storage/buffer_pool_manager.cpp`
- `src/record/rm_file_handle.h`
- `src/record/rm_file_handle.cpp`
- `src/record/rm_scan.cpp`

验证命令:

```bash
cd /mnt/e/dbms
./build/bin/unit_test
```

验证结果:

```text
[==========] 5 tests from 5 test suites ran.
[  PASSED  ] 5 tests.
```

---

## P02 - 查询执行

状态: 已完成

前置: P01

知识点: 元数据、DDL、DML、DQL、语义分析、计划生成、执行算子

### 功能范围

元数据与 DDL:

- `CREATE TABLE`
- `DROP TABLE`
- `SHOW TABLES`
- 表和字段元数据持久化

DML:

- `INSERT`
- `UPDATE`
- `DELETE`

DQL:

- `SELECT`
- 条件过滤
- 字段投影
- 多表连接

### 相关模块

- `src/system/`: 元数据与 DDL
- `src/parser/`: SQL 解析
- `src/analyze/`: 语义检查和 Query 生成
- `src/optimizer/`: 执行计划生成
- `src/execution/`: 执行算子

### 实现要求

- 完善 `sm_manager.cpp` 中除索引外的未实现函数。
- 在 analyze 模块补充 `UPDATE` 和其他缺失的语义检查。
- 对不存在的表、重复建表、不存在的字段等非法情况输出 `failure`。
- 生成顺序扫描、连接、投影、更新和删除等执行计划。
- 完成 SeqScan、NestedLoopJoin、Projection、Update、Delete 等 executor。
- 保持 INSERT executor 与其他执行模块一致。
- 浮点数输出保留 6 位小数。

### 测试点

| 测试点 | 内容 | 分值 |
| --- | --- | --- |
| 1 | 建表、删表、展示表 | 2 |
| 2 | 单表插入与条件查询 | 2 |
| 3 | 单表更新与条件查询 | 2 |
| 4 | 单表删除与条件查询 | 2 |
| 5 | 笛卡尔积和等值连接 | 4 |

代表性 SQL:

```sql
create table grade (name char(4), id int, score float);
insert into grade values ('Data', 1, 90.5);
select score, name, id from grade where score > 90;
update grade set score = 99.0 where name = 'Calc';
delete from grade where score > 90;

create table t (id int, t_name char(3));
create table d (d_name char(5), id int);
select * from t, d;
select t.id, t_name, d_name from t, d where t.id = d.id;
```

### 输出要求

以数据库名 `execution_test_db` 为例:

```bash
./bin/rmdb execution_test_db
```

结果写入:

```text
build/execution_test_db/output.txt
```

### 验证

- [x] DDL 测试通过。
- [x] 单表 INSERT/SELECT 测试通过。
- [x] UPDATE 测试通过。
- [x] DELETE 测试通过。
- [x] 多表连接测试通过。
- [x] 非法 SQL 统一输出 `failure`。

### 实现记录

完成日期: 2026-06-25

主要修改:

- `src/system/sm_manager.cpp`
- `src/analyze/analyze.cpp`
- `src/optimizer/planner.cpp`
- `src/execution/executor_abstract.h`
- `src/execution/executor_seq_scan.h`
- `src/execution/executor_projection.h`
- `src/execution/executor_nestedloop_join.h`
- `src/execution/executor_update.h`
- `src/execution/executor_delete.h`
- `src/execution/executor_insert.h`
- `src/transaction/transaction_manager.cpp`
- `src/rmdb.cpp`
- `tests/p02_smoke.py`

验证方式:

```bash
./build/bin/rmdb execution_test_db
python3 tests/p02_smoke.py ddl query update delete join
python3 tests/p02_smoke.py persistence invalid
```

验证结果:

- 五类题目 SQL 测试通过。
- 服务端重启后表定义和记录仍可读取。
- 重复建表、删除不存在的表、查询不存在的字段和语法错误均向 `output.txt` 写入 `failure`。
- `unit_test` 5/5 通过。
- CTest parser 1/1 通过。

---

## P03 - BIGINT 类型

状态: 已完成

前置: P02

知识点: 类型系统、记录存储、查询处理

### 功能要求

- 新增有符号 `BIGINT` 字段类型。
- 存储大小为 8 字节。
- 取值范围:

```text
-9223372036854775808 ~ 9223372036854775807
```

- 支持 BIGINT 字段的建表、插入、删除、更新、查询和条件比较。
- 解析整数常量时不能先以 32 位整数保存。
- 超出范围的值应输出 `failure`，且不得插入记录。

代表性 SQL:

```sql
create table t(bid bigint, sid int);
insert into t values(372036854775807, 233421);
insert into t values(-922337203685477580, 124332);
select * from t;
insert into t values(9223372036854775809, 12345);
```

最后一条 INSERT 应输出:

```text
failure
```

### 验证

- [x] BIGINT parser 支持完成。
- [x] BIGINT Value 和记录序列化完成。
- [x] BIGINT 比较和输出完成。
- [x] 最大值、最小值和溢出测试通过。
- [x] BIGINT 增删改查通过。

### 实现记录

完成日期: 2026-06-25

主要修改:

- `src/defs.h`
- `src/parser/ast.h`
- `src/parser/ast_printer.h`
- `src/parser/lex.l`
- `src/parser/yacc.y`
- `src/common/common.h`
- `src/analyze/analyze.cpp`
- `src/optimizer/planner.h`
- `src/execution/executor_abstract.h`
- `src/execution/executor_insert.h`
- `src/execution/execution_manager.cpp`
- `src/index/ix_index_handle.h`
- `tests/p03_bigint.py`

验证方式:

```bash
./build/bin/rmdb bigint_test_db
python3 tests/p03_bigint.py
python3 tests/p03_bigint.py persistence
```

验证结果:

- 题目示例的合法 BIGINT 插入与查询通过。
- `INT64_MIN` 和 `INT64_MAX` 插入、比较与持久化通过。
- 超过 int64 范围的字面量输出 `failure`，且不写入记录。
- 超过 INT 范围的值不能写入 INT 字段。
- BIGINT 查询、更新和删除通过。
- P02 端到端 SQL 回归通过。
- `unit_test` 5/5、CTest parser 1/1 通过。

---

## P04 - DATETIME 类型

状态: 已完成

前置: P02

知识点: 类型系统、日期校验、记录存储、查询处理

### 数据格式

- 大小: 8 字节。
- 文本格式: `YYYY-MM-DD HH:MM:SS`。
- 最小值: `1000-01-01 00:00:00`。
- 最大值: `9999-12-31 23:59:59`。

### 功能要求

- 支持 DATETIME 字段的建表、插入、删除、更新、查询和比较。
- 严格校验固定长度与分隔符。
- 校验月份、日期、时、分、秒。
- 正确处理大小月、二月和闰年。
- 非法值输出 `failure`，且不得修改表数据。

非法情况包括但不限于:

- 年份超出允许范围。
- 月份不在 `01-12`。
- 日期为 0 或超过当月天数。
- `1999-02-30` 等不存在的日期。
- 小时大于 23。
- 分钟或秒大于 59。
- `1999-1-07 12:30:00` 等长度不匹配的输入。

代表性 SQL:

```sql
create table t(id int, time datetime);
insert into t values(1, '2023-05-18 09:12:19');
delete from t where time = '2023-05-30 12:34:32';
update t set id = 2023 where time = '2023-05-18 09:12:19';
select * from t;
```

### 验证

- [x] DATETIME parser 支持完成。
- [x] DATETIME 内部表示与序列化完成。
- [x] DATETIME 合法性校验完成。
- [x] DATETIME 比较和输出完成。
- [x] 合法与非法日期测试通过。

### 实现记录

主要实现文件:

- `src/common/datetime.h`
- `src/common/common.h`
- `src/common/config.h`
- `src/parser/lex.l`
- `src/parser/yacc.y`
- `src/parser/ast.h`
- `src/analyze/analyze.cpp`
- `src/execution/execution_defs.h`
- `src/execution/executor_abstract.h`
- `src/execution/executor_insert.h`
- `src/execution/execution_manager.cpp`
- `src/index/ix_compare.cpp`

验证方式:

- `tests/p04_datetime.py`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`
- 启动 `./build/bin/rmdb datetime_test_db` 后执行 DATETIME SQL 脚本。
- 关闭并重启服务后执行 `python3 tests/p04_datetime.py persistence`。

验证结果:

- 固定格式、年份范围、月份、日期、时分秒和格里高利闰年校验通过。
- 最小值、最大值、`2000-02-29` 等合法边界通过。
- 非法 INSERT 和 UPDATE 均输出 `failure`，且不会修改原记录。
- DATETIME 的增删改查、比较、输出和重启持久化通过。
- P01 `unit_test` 5/5、CTest parser 1/1、P02/P03 端到端回归通过。

---

## P05 - 唯一索引

状态: 已完成

前置: P02

知识点: B+ 树、唯一约束、联合索引、索引扫描

### 功能范围

- 创建单列和多列唯一索引。
- 删除单列和多列索引。
- 展示指定表的索引。
- 使用索引完成单点查询和范围查询。
- INSERT、DELETE、UPDATE 时同步维护索引。
- 检查唯一性约束。

推荐完善 `src/index/` 中的 B+ 树，并实现 `src/system/` 中的 create/drop index。

### 1. 索引 DDL 与展示

分值: 3

支持:

```sql
create index warehouse (id);
create index warehouse (id, name);
show index from warehouse;
drop index warehouse (id);
```

输出格式:

```text
| warehouse | unique | (id) |
| warehouse | unique | (id,name) |
```

联合字段之间的逗号后不能有空格。

### 2. 索引查询

分值: 9

- 支持等值查询和范围查询。
- 联合索引遵循最左匹配原则。
- planner 应能调整 WHERE 条件顺序。

对于索引 `(id, name, score)`，以下条件均应考虑索引扫描:

```sql
where id = 1 and name = 'abcd' and score = 99.0
where id = 1 and name = 'abcd' and score > 90.0
where id = 1 and name = 'abcd'
where name = 'abcd' and id = 1
where id = 1
where id > 1
```

### 3. 索引维护

分值: 6

- INSERT 时写入所有相关索引。
- DELETE 时删除所有相关索引项。
- UPDATE 时删除旧索引项并插入新索引项。
- 操作失败时避免基表与索引状态不一致。
- 创建唯一索引或写入数据时检查重复键。

### 性能要求

对于数千条查询，建立索引后的执行时间应小于无索引执行时间的 70%，否则不视为真正使用了索引。

索引实现不限于 B+ 树。测试点 2 和 3 不要求输出行顺序与示例完全一致，但列顺序必须一致。

### 验证

- [x] 单列索引 DDL 通过。
- [x] 联合索引 DDL 通过。
- [x] `SHOW INDEX` 格式通过。
- [x] 单点查询使用索引。
- [x] 范围查询使用索引。
- [x] 最左匹配通过。
- [x] INSERT/DELETE/UPDATE 索引同步通过。
- [x] 唯一性约束通过。
- [x] 索引性能测试通过。

### 实现记录

主要实现文件:

- `src/index/ix_index_handle.cpp`
- `src/index/ix_manager.h`
- `src/index/ix_scan.cpp`
- `src/system/sm_manager.cpp`
- `src/system/sm_meta.h`
- `src/optimizer/planner.cpp`
- `src/execution/executor_index_scan.h`
- `src/execution/executor_insert.h`
- `src/execution/executor_delete.h`
- `src/execution/executor_update.h`
- `src/parser/ast_printer.h`

验证方式:

- `tests/p05_index.py`
- `python3 tests/p05_index.py stress`
- `python3 tests/p05_index.py performance`
- 关闭并重启服务后执行 `python3 tests/p05_index.py persistence`。

验证结果:

- 单列、联合唯一索引的创建、展示、删除和持久化通过。
- 等值、范围、联合索引最左匹配及乱序 WHERE 条件查询通过。
- INSERT、DELETE、UPDATE 的索引同步和重复键拒绝通过。
- 在已有重复数据上创建唯一索引会失败，且不会留下索引元数据或残缺索引文件。
- B+ 树 800 条记录分裂、全部删除、节点重分配/合并、根收缩和空树重新插入通过。
- 3000 行数据、400 次点查测试中，索引扫描与顺序扫描耗时比约为 `0.333`，满足小于 `0.7` 的要求。
- P01 `unit_test` 5/5、CTest parser 1/1、P02-P04 端到端回归通过。

---

## P06 - 聚合函数

状态: 已完成

前置: P02

知识点: SQL 解析、聚合执行

### 功能要求

支持:

- `COUNT(*)`
- `COUNT(column)`
- `MAX(column)`
- `MIN(column)`
- `SUM(column)`
- 聚合表达式的 `AS` 别名
- 聚合前的 WHERE 过滤

类型范围:

- COUNT: 行或任意支持字段。
- MAX/MIN: INT、FLOAT、CHAR。
- SUM: INT、FLOAT。

输出要求:

- 整数聚合结果不显示小数。
- 浮点数聚合结果保留 6 位小数。
- 输出列名与 SQL 中的别名一致。

测试点:

| 测试点 | 内容 | 分值 |
| --- | --- | --- |
| 1 | SUM、格式和别名 | 1 |
| 2 | MAX、MIN | 2 |
| 3 | COUNT(column)、COUNT(*) | 1 |

代表性 SQL:

```sql
select sum(id) as sum_id from aggregate;
select sum(val) as sum_val from aggregate;
select max(id) as max_id from aggregate;
select min(val) as min_val from aggregate;
select count(*) as count_row from aggregate;
select count(name) as count_name from aggregate where val = 2.0;
```

### 验证

- [x] parser 支持聚合表达式和别名。
- [x] 聚合 plan/executor 完成。
- [x] COUNT 测试通过。
- [x] MAX/MIN 测试通过。
- [x] SUM 及数值格式测试通过。

### 实现记录

主要实现文件:

- `src/parser/lex.l`
- `src/parser/yacc.y`
- `src/parser/ast.h`
- `src/parser/ast_printer.h`
- `src/analyze/analyze.h`
- `src/analyze/analyze.cpp`
- `src/common/common.h`
- `src/optimizer/plan.h`
- `src/optimizer/planner.cpp`
- `src/execution/executor_aggregate.h`
- `src/portal.h`

验证方式:

- `tests/p06_aggregate.py`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

验证结果:

- `COUNT(*)`、`COUNT(column)`、`MAX`、`MIN`、`SUM` 和多个聚合选择项通过。
- 聚合前 WHERE 过滤和索引扫描过滤通过。
- INT 聚合结果按整数输出，FLOAT 聚合结果保留 6 位小数。
- CHAR 的 MAX/MIN 和 `AS` 输出别名通过。
- 空结果集的 COUNT 输出 0。
- SUM 非数值字段、查询不存在的表等非法 SQL 输出 `failure`。
- P01 `unit_test` 5/5、CTest parser 1/1、P02-P05 端到端与索引压力回归通过。

---

## P07 - ORDER BY 与 LIMIT

状态: 已完成

前置: P02

知识点: SQL 解析、排序算子、查询执行

### 功能要求

- 支持单列排序。
- 支持多列排序。
- 默认使用升序。
- 支持每个排序字段独立指定 `ASC` 或 `DESC`。
- 支持 `LIMIT` 限制最终结果数量。
- LIMIT 应在排序后生效。

代表性 SQL:

```sql
select company, order_number
from orders
order by order_number;

select company, order_number
from orders
order by company desc, order_number asc;

select company, order_number
from orders
order by order_number asc
limit 2;
```

### 验证

- [x] parser 支持 ORDER BY。
- [x] parser 支持 LIMIT。
- [x] 单列 ASC/DESC 排序通过。
- [x] 多列混合方向排序通过。
- [x] LIMIT 与排序组合通过。

### 实现记录

主要实现文件:

- `src/parser/lex.l`
- `src/parser/yacc.y`
- `src/parser/ast.h`
- `src/optimizer/plan.h`
- `src/optimizer/planner.cpp`
- `src/execution/execution_sort.h`
- `src/portal.h`

验证方式:

- `tests/p07_order_limit.py`
- `cmake --build build`
- `ctest --test-dir build --output-on-failure`

验证结果:

- 单列默认 ASC、显式 ASC 和 DESC 排序通过。
- 多列排序及每列独立 ASC/DESC 通过。
- 支持按未出现在 SELECT 投影列表中的字段排序。
- LIMIT 在排序后执行，LIMIT 0 和不带 ORDER BY 的 LIMIT 通过。
- 不存在的排序字段和负数 LIMIT 输出 `failure`。
- 排序使用稳定比较，相同排序键保持输入相对顺序。
- P01 `unit_test` 5/5、CTest parser 1/1、P02-P06 端到端与索引压力回归通过。

---

## P08 - 事务控制语句

状态: 已完成

前置: 查询执行、唯一索引、DATETIME

知识点: 事务状态、提交、回滚

### 功能范围

支持显式事务语句:

```sql
begin;
commit;
abort;
```

### 约束

- 显式事务只包含 INSERT、DELETE、UPDATE、SELECT。
- 显式事务不包含 DDL。
- 本题不考虑并发事务。
- 需要测试有索引和无索引情况下的提交与回滚。
- 有索引测试可能包含合法 DATETIME 数据。
- 测试数据可能采用 TPC-C NewOrder 事务。

代表性 SQL:

```sql
create table student (id int, name char(8), score float);
insert into student values (1, 'xiaohong', 90.0);

begin;
insert into student values (2, 'xiaoming', 99.0);
delete from student where id = 2;
abort;

select * from student;
```

回滚后只应保留事务开始前的数据。

### 实现要求

- 管理显式事务的开始、活动、提交和终止状态。
- COMMIT 后保留数据及索引修改。
- ABORT 时逆序撤销 INSERT、DELETE、UPDATE。
- 回滚后基表、索引和元数据保持一致。
- 保留或明确处理框架中的单语句隐式事务。

### 验证

- [x] BEGIN/COMMIT parser 与执行完成。
- [x] BEGIN/ABORT parser 与执行完成。
- [x] 无索引提交和回滚通过。
- [x] 有索引提交和回滚通过。
- [x] DATETIME 数据事务通过。

### 实现记录

- 在 DML executor 中记录事务写集: INSERT 记录 rid, DELETE/UPDATE 记录修改前的完整记录。
- `TransactionManager::abort` 按写集逆序撤销 INSERT、DELETE、UPDATE，并同步维护相关唯一索引。
- 显式事务继续复用框架已有的 `begin; commit; abort; rollback;` 语法和单语句隐式事务机制。
- 新增 `tests/p08_transaction.py`，覆盖 abort 撤销插入/删除/更新、commit 保留修改、索引表和 DATETIME 字段。
- 2026-06-26 验证: 构建通过，`ctest --output-on-failure`、`./bin/unit_test`、P02-P07 回归和 P08 事务脚本通过。

---

## P09 - 可串行化与死锁预防

状态: 已完成

前置: P08、唯一索引

知识点: 两阶段封锁、多粒度锁、可串行化、No-Wait、幻读

### 功能要求

- 支持并发事务。
- 使用两阶段封锁保证可串行化隔离级别。
- 使用 No-Wait 策略预防死锁。
- 保护所有共享状态，包括事务表、缓冲池、基表和索引。

需要避免的数据异常:

- 脏写
- 脏读
- 丢失更新
- 不可重复读
- 幻读

### 锁要求

- 根据读写操作获取共享锁或排他锁。
- 支持必要的表级锁和记录级锁。
- 可通过表级锁避免幻读。
- 为提高并发性能，可在索引中实现间隙锁。
- 锁冲突且 No-Wait 失败时，立即终止事务并释放其资源。

代表性脏读测试:

```text
T1: begin
T2: begin
T1: update ... where id = 2
T2: select ... where id = 2
T1: abort
T1: select ... where id = 2
T2: commit
```

### 输出要求

- 不得修改 `src/record_printer.h` 的记录格式。
- 因 No-Wait 策略回滚事务时，返回:

```text
abort\n
```

- 除 SELECT 结果和事务回滚信息外，不向客户端返回无关内容。

### 验证

- [x] 锁管理器并发安全。
- [x] 事务表并发安全。
- [x] 基表和索引并发安全。
- [x] 两阶段封锁规则实现。
- [x] No-Wait 回滚实现。
- [x] 五类数据异常测试通过。
- [x] 幻读测试通过。

### 实现记录

- 实现 `LockManager` 的全局锁表、S/X 锁兼容性检查、重入、S 到 X 升级、unlock 和 No-Wait 冲突中止。
- SELECT 扫描获取表级 S 锁，INSERT/DELETE/UPDATE 获取表级 X 锁；UPDATE/DELETE 在生成目标 rid 前先加 X 锁，避免扫描阶段产生升级空窗。
- 使用表级 S/X 锁保守保证可串行化，并通过表锁避免幻读；当前未实现更细粒度的间隙锁优化。
- No-Wait 冲突抛出 `TransactionAbortException`，服务端返回 `abort\n` 并调用 P08 回滚逻辑撤销事务写集。
- 新增 `tests/p09_concurrency.py`，用两个客户端连接覆盖脏读、丢失更新、不可重复读、幻读和 No-Wait abort。
- 2026-06-26 验证: 构建通过，`ctest --output-on-failure`、`./bin/unit_test`、P02-P09 回归通过。

---

## P10 - WAL 与故障恢复

状态: 已完成

前置: P09

知识点: WAL、REDO/UNDO、ARIES、崩溃恢复

### 功能要求

- 实现日志管理器和日志缓冲区。
- 实现 WAL，保证相关数据页刷盘前日志已经持久化。
- 为数据库写操作生成 REDO/UNDO 所需日志。
- 系统重启时从日志恢复到一致状态。

### 约束

- 测试场景为系统故障。
- 不测试恢复过程中的再次故障。
- 不测试 checkpoint。
- 每次从日志文件第一条记录开始恢复。
- 索引操作应记录物理日志，避免索引结构不一致。
- 测试可能包含两表 JOIN 和 ORDER BY。

代表性场景:

```sql
create table t1 (id int, num int);

begin;
insert into t1 values(1, 1);
commit;

begin;
insert into t1 values(2, 2);
-- 系统收到终止信号并崩溃
```

重启后:

- 已提交事务的数据应存在。
- 未提交事务的数据应被撤销。
- 基表与索引状态应一致。

### 输出要求

- 单线程测试检查数据库目录下的 `output.txt`。
- 多线程测试检查客户端返回字符串。
- 输出格式沿用其他题目的要求。

### 验证

- [x] 日志记录格式完成。
- [x] 日志缓冲与刷盘完成。
- [x] WAL 顺序约束完成。
- [x] INSERT/DELETE/UPDATE REDO/UNDO 完成。
- [x] COMMIT/ABORT 日志完成。
- [x] 基表恢复完成。
- [x] 索引一致性恢复完成。
- [x] 已提交事务 REDO 测试通过。
- [x] 未提交事务 UNDO 测试通过。
- [x] 单线程和多线程恢复测试通过。

### 实现记录

- 完成 `BeginLogRecord`、`CommitLogRecord`、`AbortLogRecord`、`InsertLogRecord`、`DeleteLogRecord`、`UpdateLogRecord` 的序列化/反序列化。
- `LogManager` 支持日志缓冲、LSN 分配、刷盘和 `fsync`，事务提交/回滚会强制刷盘。
- INSERT/DELETE/UPDATE executor 生成 redo/undo 所需日志；DELETE/UPDATE 在修改基表和索引前写 WAL，INSERT 在获得 rid 后立即写 WAL。
- `RecoveryManager` 启动时扫描 `db.log`，redo 已提交事务，逆序 undo 未提交事务。
- 恢复后统一重建所有已声明索引，保证基表和索引一致；当前采用逻辑 DML 日志加索引重建，而非逐条索引物理日志。
- 新增 `tests/p10_recovery.py`，用于两阶段恢复验证: `setup` 阶段写入已提交和未提交事务并触发 `crash`，`check` 阶段重启后检查已提交保留、未提交撤销、索引查询可用。
- 2026-06-27 验证: 构建通过，`ctest --output-on-failure`、`./bin/unit_test`、P02-P10 回归通过。
- 修正隐式事务提交顺序，保证客户端收到单语句响应时该隐式事务已经提交并释放锁。
