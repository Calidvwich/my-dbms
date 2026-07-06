# RMDB DBMS Project

这是一个基于 RMDB 课程框架实现的关系型数据库管理系统。仓库已精简为最小可运行代码，只保留构建和运行服务端/客户端所需的源码与配置。

## 环境要求

推荐环境：

```text
WSL Ubuntu 18.04
CMake >= 3.16
g++ >= 7.5
flex
bison
readline
pthread
```

在 WSL 中安装依赖：

```bash
sudo apt update
sudo apt install -y build-essential gcc g++ make git flex bison libreadline-dev
```

如果 Ubuntu 18.04 自带的 CMake 版本低于 3.16，需要额外安装新版 CMake。

## 构建

进入项目目录：

```bash
cd /mnt/e/dbms
```

执行干净构建：

```bash
rm -rf build
mkdir build
cd build
cmake ..
make -j"$(nproc)"
```

构建完成后会生成：

```text
build/bin/rmdb
build/bin/rmdb_client
```

如果评测脚本需要编译存储管理单元测试，可以执行：

```bash
make unit_test
./bin/unit_test
```

## 运行

启动服务端：

```bash
cd /mnt/e/dbms/build
./bin/rmdb test_db
```

另开一个 WSL 终端，启动客户端：

```bash
cd /mnt/e/dbms/build
./bin/rmdb_client
```

客户端中可以输入 SQL，例如：

```sql
create table t(id int, name char(8));
insert into t values(1, 'alice');
select * from t;
```

退出客户端：

```sql
exit;
```

停止服务端：在服务端终端按 `Ctrl+C`。

## 输出文件

SQL 查询结果会写入数据库目录下的 `output.txt`，例如：

```text
build/test_db/output.txt
```

## 项目结构

```text
.
|-- CMakeLists.txt
|-- License
|-- README.md
|-- rmdb_client/      客户端
`-- src/              数据库内核源码
    |-- analyze/      语义分析
    |-- common/       公共定义
    |-- execution/    查询执行
    |-- index/        B+ 树索引
    |-- optimizer/    查询计划
    |-- parser/       SQL 解析
    |-- record/       记录管理
    |-- recovery/     日志与恢复
    |-- replacer/     缓冲池替换
    |-- storage/      磁盘与缓冲池
    |-- system/       元数据管理
    `-- transaction/  事务与并发控制
```

## 说明

本仓库不包含测试脚本、题目整理文档、参考 PDF、图片或本地 WSL 环境文件。`build/`、`project/`、`references/`、`wsl/` 等本地产物已加入 `.gitignore`。
