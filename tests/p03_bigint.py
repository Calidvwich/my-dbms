#!/usr/bin/env python3

import socket
import sys


COMMANDS = [
    "create table t(bid bigint,sid int);",
    "insert into t values(372036854775807,233421);",
    "insert into t values(-922337203685477580,124332);",
    "select * from t;",
    "insert into t values(9223372036854775809,12345);",
    "select * from t;",
    "insert into t values(9223372036854775807,1);",
    "insert into t values(-9223372036854775808,2);",
    "select bid from t where bid > 0;",
    "update t set bid = 7 where sid = 1;",
    "delete from t where bid = -9223372036854775808;",
    "select * from t;",
    "create table ints(id int);",
    "insert into ints values(2147483648);",
    "select * from ints;",
]

PERSISTENCE_COMMANDS = [
    "select * from t;",
    "select * from t where bid = 7;",
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
