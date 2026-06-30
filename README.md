# RMDB DBMS Project

本项目基于 RMDB 课程框架进行二次开发，目标是在 WSL Ubuntu 18.04 环境中实现一个可编译、可运行、可测试的关系型数据库管理系统。当前代码已经从原参考框架迁移到仓库根目录，后续题目需求整理在 `problems.md`，实现进度记录在 `TODO.md`。

## 功能范围

当前实现覆盖课程基础功能题的主要模块：

- 存储管理：磁盘管理器、缓冲池、LRU 替换、记录管理。
- 查询执行：DDL、DML、DQL、条件查询、连接查询。
- 数据类型：`BIGINT`、`DATETIME`。
- 索引：唯一索引、联合索引、最左匹配、范围查询、DML 同步维护。
- 聚合：`COUNT`、`MAX`、`MIN`、`SUM`、`AS` 别名。
- 排序限制：多列 `ORDER BY`、`ASC` / `DESC`、`LIMIT`。
- 事务：显式 `begin` / `commit` / `abort`。
- 并发控制：两阶段封锁、No-Wait 死锁预防。
- 故障恢复：WAL、REDO/UNDO、崩溃后恢复一致状态。

## 从零配置环境

以下步骤面向 Windows + WSL，推荐使用 Ubuntu 18.04，并将 WSL 发行版命名为 `my-dbms`。本仓库的本地 WSL 文件夹 `wsl/` 已加入 `.gitignore`，不会被提交。

### 1. Clone 仓库

在 Windows PowerShell 中进入你希望放置项目的位置：

```powershell
cd E:\
git clone <your-repo-url> dbms
cd E:\dbms
```

如果已经 clone 过，只需要更新：

```powershell
cd E:\dbms
git pull
```

### 2. 准备 WSL Ubuntu 18.04

如果本机已经有 `my-dbms`：

```powershell
wsl -d my-dbms
```

如果还没有，需要先安装或导入一个 Ubuntu 18.04 发行版。推荐命名为 `my-dbms`，这样后续命令可以保持一致。

一种常见导入方式如下，其中 `<ubuntu-18.04-rootfs.tar.gz>` 替换为你准备好的 Ubuntu 18.04 rootfs 包：

```powershell
mkdir E:\dbms\wsl
wsl --import my-dbms E:\dbms\wsl\my-dbms <ubuntu-18.04-rootfs.tar.gz>
wsl -d my-dbms
```

进入 WSL 后，项目路径通常是：

```bash
cd /mnt/e/dbms
```

如果你的仓库不在 `E:\dbms`，把路径换成对应的 `/mnt/<drive>/<path>`。

### 3. 安装依赖

在 WSL 中执行：

```bash
sudo apt update
sudo apt install -y build-essential gcc g++ make git flex bison libreadline-dev python3
```

项目要求 CMake 版本不低于 `3.16`。先检查：

```bash
cmake --version
```

如果版本已经是 `3.16` 或更高，可以跳过下面的 CMake 安装。如果 Ubuntu 18.04 默认源中的 CMake 版本过低，可以安装 CMake 3.16.9：

```bash
cd /tmp
wget https://github.com/Kitware/CMake/releases/download/v3.16.9/cmake-3.16.9-Linux-x86_64.sh
chmod +x cmake-3.16.9-Linux-x86_64.sh
sudo mkdir -p /opt/cmake-3.16.9
sudo ./cmake-3.16.9-Linux-x86_64.sh --prefix=/opt/cmake-3.16.9 --skip-license
echo 'export PATH=/opt/cmake-3.16.9/bin:$PATH' >> ~/.bashrc
source ~/.bashrc
cmake --version
```

确认工具版本：

```bash
g++ --version
flex --version
bison --version
python3 --version
```

当前已验证过的环境：

```text
Ubuntu 18.04.2 LTS
cmake 3.16.9
g++ 7.5.0
flex 2.6.4
bison 3.0.4
git 2.17.1
```

### 4. 检查第三方依赖

仓库需要 `deps/googletest` 用于单元测试。clone 或 pull 后检查：

```bash
ls deps/googletest
```

如果该目录为空，优先执行：

```bash
git submodule update --init --recursive
```

如果本仓库没有使用 submodule，而是直接提交了 `deps/googletest`，则无需额外操作。

## 构建

在 WSL 中执行：

```bash
cd /mnt/e/dbms
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
```

构建产物位于：

```text
build/bin/rmdb
build/bin/unit_test
```

如果 `git pull` 后出现奇怪的编译问题，建议做一次干净构建：

```bash
cd /mnt/e/dbms
rm -rf build
mkdir build
cd build
cmake ..
make -j"$(nproc)"
```

## 运行测试

### 单元测试

```bash
cd /mnt/e/dbms/build
./bin/unit_test
ctest --output-on-failure
```

### 端到端 SQL 测试

大多数脚本需要先启动服务端，再运行 Python 客户端脚本。示例：

```bash
cd /mnt/e/dbms/build
./bin/rmdb p02_test_db
```

另开一个 WSL 终端：

```bash
cd /mnt/e/dbms
python3 tests/p02_smoke.py
```

常用测试脚本：

```text
tests/p02_smoke.py        查询执行
tests/p03_bigint.py       BIGINT 类型
tests/p04_datetime.py     DATETIME 类型
tests/p05_index.py        唯一索引
tests/p06_aggregate.py    聚合函数
tests/p07_order_limit.py  ORDER BY / LIMIT
tests/p08_transaction.py  显式事务
tests/p09_concurrency.py  并发控制
tests/p10_recovery.py     故障恢复
```

故障恢复测试分为两个阶段：

```bash
cd /mnt/e/dbms/build
./bin/rmdb p10_recovery_db
```

另开终端执行：

```bash
cd /mnt/e/dbms
python3 tests/p10_recovery.py setup
```

服务端被触发崩溃后，重新启动：

```bash
cd /mnt/e/dbms/build
./bin/rmdb p10_recovery_db
```

再执行检查：

```bash
cd /mnt/e/dbms
python3 tests/p10_recovery.py check
```

## 运行数据库

构建完成后，可以在 WSL 中启动服务端：

```bash
cd /mnt/e/dbms/build
./bin/rmdb test_db
```

数据库输出文件会写入对应数据库目录，例如：

```text
build/test_db/output.txt
```

如果需要清理某个测试数据库：

```bash
cd /mnt/e/dbms/build
rm -rf test_db
```

## 项目结构

```text
.
|-- CMakeLists.txt
|-- README.md
|-- TODO.md
|-- problems.md
|-- deps/
|   `-- googletest/
|-- rmdb_client/
|-- src/
|   |-- analyze/       语义分析
|   |-- common/        公共配置和上下文
|   |-- execution/     执行器和执行管理
|   |-- index/         B+ 树索引
|   |-- optimizer/     查询计划生成
|   |-- parser/        flex/bison SQL 解析器
|   |-- record/        记录管理
|   |-- recovery/      日志与故障恢复
|   |-- replacer/      缓冲池替换策略
|   |-- storage/       磁盘和缓冲池管理
|   |-- system/        数据库、表、索引元数据管理
|   |-- test/          原框架测试
|   `-- transaction/   事务和并发控制
|-- tests/             当前补充的端到端测试脚本
|-- *.pdf              课程与 RMDB 参考文档
`-- project/           本地参考框架副本，已忽略
```

## 文档说明

- `README.md`：项目说明、环境配置、构建和测试入口。
- `TODO.md`：任务拆分、实现进度和验证记录。
- `problems.md`：题目原文、需求整理、实现说明。
- `RMDB使用文档.pdf`：RMDB 原始使用说明。
- `RMDB环境配置文档.pdf`：RMDB 原始环境说明。
- `RMDB项目结构.pdf`：RMDB 原始结构说明。
- `测试说明文档.pdf`：课程测试说明。

## 注意事项

- `wsl/`、`project/`、`references/`、`build/` 都是本地辅助或构建目录，已加入 `.gitignore`。
- 评测通常要求输出写入数据库目录下的 `output.txt`，例如 `build/execution_test_db/output.txt`。
- 修改 parser 相关文件后，建议重新执行干净构建，避免 flex/bison 生成文件残留造成误判。
- 每完成一道题，建议同步更新 `TODO.md` 和 `problems.md` 中的实现与验证记录。
