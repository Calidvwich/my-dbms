#!/usr/bin/env python3

import socket
import sys
import time


COMMANDS = [
    "create table warehouse(id int,name char(4),score float);",
    "insert into warehouse values(1,'aaaa',10.0);",
    "insert into warehouse values(2,'bbbb',20.0);",
    "insert into warehouse values(3,'cccc',30.0);",
    "create index warehouse(id);",
    "create index warehouse(name,score);",
    "show index from warehouse;",
    "select * from warehouse where id = 2;",
    "select * from warehouse where id > 1;",
    "select * from warehouse where name = 'bbbb' and score >= 20.0;",
    "select * from warehouse where score >= 20.0 and name = 'bbbb';",
    "insert into warehouse values(2,'dddd',40.0);",
    "insert into warehouse values(4,'bbbb',20.0);",
    "update warehouse set id = 2 where id = 3;",
    "select * from warehouse;",
    "delete from warehouse where id = 2;",
    "insert into warehouse values(2,'dddd',40.0);",
    "update warehouse set name = 'zzzz',score = 99.0 where id = 2;",
    "select * from warehouse where name = 'zzzz' and score = 99.0;",
    "drop index warehouse(id);",
    "show index from warehouse;",
    "create table duplicate_keys(id int);",
    "insert into duplicate_keys values(1);",
    "insert into duplicate_keys values(1);",
    "create index duplicate_keys(id);",
]

PERSISTENCE_COMMANDS = [
    "show index from warehouse;",
    "select * from warehouse where name = 'zzzz' and score = 99.0;",
]

STRESS_SETUP = [
    "create table stress(id int,payload char(8));",
    "create index stress(id);",
]


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    return stream.recv(8192).rstrip(b"\0").decode("utf-8", errors="replace")


def main():
    mode = sys.argv[1:] 
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        if mode == ["stress"]:
            for sql in STRESS_SETUP:
                send_sql(stream, sql)
            for value in range(800):
                response = send_sql(
                    stream, f"insert into stress values({value},'v{value:07d}');"
                )
                if "Error" in response or "failure" in response:
                    raise RuntimeError(f"insert {value} failed: {response}")
            checks = {
                "select * from stress where id = 511;": "Total record(s): 1",
                "select * from stress where id >= 390 and id < 410;": "Total record(s): 20",
            }
            for sql, expected in checks.items():
                response = send_sql(stream, sql)
                if expected not in response:
                    raise RuntimeError(f"{sql} expected {expected}, got:\n{response}")
            for value in range(700):
                response = send_sql(stream, f"delete from stress where id = {value};")
                if "Error" in response or "failure" in response:
                    raise RuntimeError(f"delete {value} failed: {response}")
            response = send_sql(stream, "select * from stress where id >= 700;")
            if "Total record(s): 100" not in response:
                raise RuntimeError(f"post-delete range scan failed:\n{response}")
            response = send_sql(stream, "select * from stress where id = 750;")
            if "Total record(s): 1" not in response:
                raise RuntimeError(f"post-delete point scan failed:\n{response}")
            for value in range(700, 800):
                response = send_sql(stream, f"delete from stress where id = {value};")
                if "Error" in response or "failure" in response:
                    raise RuntimeError(f"final delete {value} failed: {response}")
            response = send_sql(stream, "select * from stress where id >= 0;")
            if "Total record(s): 0" not in response:
                raise RuntimeError(f"empty-tree scan failed:\n{response}")
            response = send_sql(stream, "insert into stress values(42,'reinsert');")
            if "Error" in response or "failure" in response:
                raise RuntimeError(f"reinsert into empty tree failed: {response}")
            response = send_sql(stream, "select * from stress where id = 42;")
            if "Total record(s): 1" not in response:
                raise RuntimeError(f"reinsert lookup failed:\n{response}")
            print("P05 stress test passed")
            return
        if mode == ["performance"]:
            send_sql(stream, "create table seq_data(id int,payload char(8));")
            send_sql(stream, "create table idx_data(id int,payload char(8));")
            for value in range(3000):
                payload = f"v{value:07d}"
                send_sql(stream, f"insert into seq_data values({value},'{payload}');")
                send_sql(stream, f"insert into idx_data values({value},'{payload}');")
            send_sql(stream, "create index idx_data(id);")

            def elapsed(table):
                start = time.perf_counter()
                for value in range(400):
                    target = (value * 7919) % 3000
                    response = send_sql(stream, f"select * from {table} where id = {target};")
                    if "Total record(s): 1" not in response:
                        raise RuntimeError(f"point lookup failed for {table}.{target}")
                return time.perf_counter() - start

            seq_time = elapsed("seq_data")
            index_time = elapsed("idx_data")
            ratio = index_time / seq_time
            print(
                f"P05 performance: seq={seq_time:.3f}s index={index_time:.3f}s "
                f"ratio={ratio:.3f}"
            )
            if ratio >= 0.7:
                raise RuntimeError("index lookup did not meet the 70% performance threshold")
            return

        commands = PERSISTENCE_COMMANDS if mode == ["persistence"] else COMMANDS
        for sql in commands:
            response = send_sql(stream, sql)
            print(f"SQL> {sql}")
            if response:
                print(response, end="" if response.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
