#!/usr/bin/env python3

import socket
import sys


COMMANDS = [
    "create table t(id int,time datetime);",
    "insert into t values(1,'2023-05-18 09:12:19');",
    "insert into t values(2,'2023-05-30 12:34:32');",
    "select * from t;",
    "delete from t where time = '2023-05-30 12:34:32';",
    "update t set id = 2023 where time = '2023-05-18 09:12:19';",
    "select * from t;",
    "drop table t;",
    "create table t(time datetime,temperature float);",
    "insert into t values('1999-07-07 12:30:00',36.0);",
    "select * from t;",
    "insert into t values('1999-13-07 12:30:00',36.0);",
    "insert into t values('1999-1-07 12:30:00',36.0);",
    "insert into t values('1999-00-07 12:30:00',36.0);",
    "insert into t values('1999-07-00 12:30:00',36.0);",
    "insert into t values('0001-07-10 12:30:00',36.0);",
    "insert into t values('1999-02-30 12:30:00',36.0);",
    "insert into t values('1999-02-28 12:30:61',36.0);",
    "insert into t values('2000-02-29 23:59:59',1.0);",
    "insert into t values('1900-02-29 00:00:00',1.0);",
    "insert into t values('1000-01-01 00:00:00',2.0);",
    "insert into t values('9999-12-31 23:59:59',3.0);",
    "update t set time = '2001-02-29 00:00:00' where temperature = 36.0;",
    "select * from t where time >= '2000-01-01 00:00:00';",
    "select * from t;",
]

PERSISTENCE_COMMANDS = [
    "select * from t;",
    "select * from t where time = '2000-02-29 23:59:59';",
]


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    return stream.recv(8192).rstrip(b"\0").decode("utf-8", errors="replace")


def main():
    commands = PERSISTENCE_COMMANDS if sys.argv[1:] == ["persistence"] else COMMANDS
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        for sql in commands:
            response = send_sql(stream, sql)
            print(f"SQL> {sql}")
            if response:
                print(response, end="" if response.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
