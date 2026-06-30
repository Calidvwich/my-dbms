# TODO

## 基础迁移与环境

- [x] 将 `project/rmdb/` 框架内容提升到仓库根目录。
- [x] 将 `project/` 加入 `.gitignore`，保留为本地参考副本。
- [x] 将 `wsl/` 加入 `.gitignore`，避免追踪本地 WSL 虚拟磁盘。
- [x] 在 WSL 中配置 `my-dbms` Ubuntu 18.04 开发环境。
- [x] 安装并验证 CMake、G++、flex、bison、readline 等依赖。
- [x] 在 WSL 中完成一次干净构建验证。
- [x] 在 WSL 中运行 `unit_test` 并记录当前基线结果。
- [ ] 增加 `scripts/build.sh`。
- [ ] 增加 `scripts/test.sh`。
- [ ] 增加 `scripts/run.sh`。

## 题目管理

- [x] 新建并维护 `problems.md`，用于收集后续题目。
- [ ] 每次粘贴网页题目后，删除平台导航、登录提示、页脚、广告、重复文案等不相关内容。
- [ ] 为每道题整理功能目标、输入输出、约束条件和验收点。
- [ ] 为每道题建立实现任务列表。
- [ ] 为每道题补充最小 SQL 或单元测试用例。
- [ ] 每完成一道题后，在 `problems.md` 中记录实现文件和验证结果。

## 框架熟悉

- [ ] 阅读 `RMDB使用文档.pdf`。
- [ ] 阅读 `RMDB环境配置文档.pdf`。
- [ ] 阅读 `RMDB项目结构.pdf`。
- [ ] 阅读 `测试说明文档.pdf`。
- [ ] 梳理 `src/storage/` 的磁盘与缓冲池接口。
- [ ] 梳理 `src/record/` 的记录页、文件句柄和扫描逻辑。
- [ ] 梳理 `src/index/` 的索引结构和扫描逻辑。
- [ ] 梳理 `src/system/` 的 database、table、index 元数据管理。
- [ ] 梳理 `src/parser/` 的 flex/bison 语法支持范围。
- [ ] 梳理 `src/analyze/` 的语义检查逻辑。
- [ ] 梳理 `src/optimizer/` 的 planner 和 plan node。
- [ ] 梳理 `src/execution/` 的 executor 体系。
- [ ] 梳理 `src/transaction/` 和 `src/recovery/` 的现有实现空缺。

## 已知待补功能

- [x] 完成 P03 BIGINT 类型的 parser、存储、比较、输出和越界检查。
- [x] 完成 P04 DATETIME 类型的 parser、校验、存储、比较、输出和持久化。
- [x] 完成 P05 唯一索引 DDL、查询扫描、DML 同步、唯一性检查和持久化。
- [x] 完成 P06 COUNT、MAX、MIN、SUM、AS 别名和聚合前 WHERE 过滤。
- [x] 完成 P07 多列 ORDER BY、独立 ASC/DESC 和 LIMIT。
- [x] 完成 P08 显式事务 begin/commit/abort、DML 写集记录和基表/索引回滚。
- [x] 完成 P09 表级 S/X 两阶段封锁、No-Wait 回滚和并发异常防护。
- [x] 验证 P10 WAL 与故障恢复端到端流程。
- [x] 完成 `src/storage/disk_manager.cpp` 的文件与页面读写接口。
- [x] 完成 `src/replacer/lru_replacer.cpp`，`LRUReplacerTest.SampleTest` 已通过。
- [x] 完成 `src/storage/buffer_pool_manager.cpp`，基础与并发测试已通过。
- [x] 完成 `src/record/rm_file_handle.cpp` 的记录增删改查与空闲页管理。
- [x] 完成 `src/record/rm_scan.cpp` 的记录扫描。
- [x] 完善 `Analyze::do_analyze` 中表存在性检查。
- [x] 完善 `Analyze::do_analyze` 中 select 目标列检查。
- [x] 完善 `Analyze::do_analyze` 中 where 条件列检查。
- [x] 完善 `Analyze::check_clause` 的列解析和类型检查。
- [x] 完成 P02 所需的基础查询计划生成。
- [x] 检查 `execution_sort.h` 的排序 executor 行为。
- [x] 完成 `executor_seq_scan.h` 的顺序扫描与条件过滤。
- [x] 检查 `executor_index_scan.h` 的索引扫描边界条件。
- [x] 完成 `executor_projection.h` 的投影 executor。
- [x] 完成 `executor_nestedloop_join.h` 的笛卡尔积与条件连接。
- [x] 完成 `executor_update.h` 的更新 executor。
- [x] 完成 `executor_delete.h` 的删除 executor。
- [x] 完成 P02 所需的插入 executor 与数值类型提升。
- [x] 检查 `rm_file_handle.cpp` 的记录读取、插入、删除和更新逻辑。
- [x] 检查 `buffer_pool_manager.cpp` 的页面分配、获取、淘汰和刷盘逻辑。
- [x] 检查 `ix_index_handle.cpp` 的 B+ 树查找、插入、删除、分裂和合并逻辑。
- [x] 检查 `ix_scan.cpp` 的索引遍历逻辑。
- [x] 补齐 `transaction_manager.cpp` 的显式事务提交与逆序回滚。
- [x] 补齐 `lock_manager.cpp` 的锁表、兼容性检查、升级、释放和 No-Wait 策略。
- [x] 补齐 `log_manager.h` 中 commit、abort、delete、update 日志记录。
- [x] 梳理 recovery 模块当前支持范围并补齐恢复入口。

## 测试与验收

- [x] 建立当前未修改框架的构建基线。
- [x] 建立当前未修改框架的测试基线。
- [x] 为 parser 增加 SQL 解析测试。
- [ ] 为 analyze 增加语义检查测试。
- [x] 为 storage 增加磁盘页读写测试。
- [x] 为 buffer pool 增加 pin/unpin、dirty、replacement 测试。
- [x] 为 record manager 增加插入、删除、更新、扫描测试。
- [x] 为 index 增加等值、范围、分裂、合并测试。
- [x] 为 executor 增加 insert/delete/update/select 测试。
- [x] 为聚合 executor 增加 COUNT、MAX、MIN、SUM、WHERE 和非法类型测试。
- [x] 为 transaction 增加最小显式事务提交与回滚测试。
- [x] 为并发事务增加 No-Wait、脏读、丢失更新、不可重复读和幻读测试。
- [x] 为 recovery 增加日志与故障恢复测试脚本。
- [x] 在 WSL 中运行 P10 构建和恢复脚本验证。
- [x] 根据后续题目补充端到端 SQL 脚本。

## 当前基线记录

- [x] 2026-06-23: `cmake .. && make -j$(nproc)` 在 `my-dbms` WSL 中构建成功。
- [x] 2026-06-23: `./build/bin/unit_test` 已运行，但测试未通过。
- [x] 2026-06-23: 历史基线中 `LRUReplacerTest.SampleTest` 失败，已于 2026-06-25 修复。
- [x] 2026-06-23: 历史基线中 `BufferPoolManagerTest.SampleTest` 中止，已于 2026-06-25 修复。
- [x] 2026-06-25: 完成 P01 存储管理，`unit_test` 的 5 个测试套件全部通过。
- [x] 2026-06-25: 完成 P02 查询执行，五类 SQL、持久化和非法 SQL 测试通过。
- [x] 2026-06-25: 完成 P03 BIGINT，边界值、溢出、增删改查和持久化测试通过。
- [x] 2026-06-25: 完成 P04 DATETIME，格式校验、闰年边界、增删改查和持久化测试通过。
- [x] 2026-06-25: 复查 P01-P03，修复 `RmRecord` 赋值/反序列化内存所有权、`TabMeta` 索引元数据复制和 lexer 非法字符处理；全量回归通过。
- [x] 2026-06-25: 完成 P05 唯一索引，DDL、最左匹配、范围扫描、DML 同步、B+ 树删除平衡和持久化通过；性能比值约为 0.333。
- [x] 2026-06-25: 完成 P06 聚合函数，COUNT、MAX、MIN、SUM、AS 别名、WHERE 过滤和输出格式测试通过。
- [x] 2026-06-25: 完成 P07 ORDER BY 与 LIMIT，单列、多列混合方向、未投影排序字段和 LIMIT 边界测试通过。
- [x] 2026-06-26: 完成 P08 事务控制语句，begin/commit/abort、INSERT/DELETE/UPDATE 逆序回滚、索引同步和 DATETIME 事务测试通过；P02-P07 回归通过。
- [x] 2026-06-26: 完成 P09 并发控制，表级 S/X 锁、两阶段封锁、No-Wait abort 和并发异常防护测试通过；P02-P09 回归通过。
- [x] 2026-06-27: 完成 P10 WAL 与故障恢复，已提交 REDO、未提交 UNDO、索引恢复重建和崩溃重启测试通过；P02-P10 回归通过。
