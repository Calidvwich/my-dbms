#!/usr/bin/env python3

import socket
import sys


SETUP_COMMANDS = [
    "create table t(id int,score float,tag char(4));",
    "create index t(id);",
    "begin;",
    "insert into t values(1,10.0,'ok01');",
    "commit;",
    "begin;",
    "insert into t values(2,20.0,'no02');",
    "update t set score = 99.0 where id = 1;",
    "delete from t where id = 2;",
    "crash",
]

CHECK_COMMANDS = [
    "select id,score,tag from t order by id;",
    "select id,score,tag from t where id = 1;",
    "select * from t where id = 2;",
]

EXPECTED_OUTPUT = """| id | score | tag |
| 1 | 10.000000 | ok01 |
| id | score | tag |
| 1 | 10.000000 | ok01 |
| id | score | tag |
"""


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    if sql == "crash":
        return ""
    return stream.recv(8192).rstrip(b"\0").decode("utf-8", errors="replace")


def run_setup():
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        for sql in SETUP_COMMANDS:
            send_sql(stream, sql)


def run_check():
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        for sql in CHECK_COMMANDS:
            response = send_sql(stream, sql)
            print(f"SQL> {sql}")
            if response:
                print(response, end="" if response.endswith("\n") else "\n")
    with open("build/p10_recovery_db/output.txt", "r", encoding="utf-8") as output_file:
        actual = output_file.read()
    if actual != EXPECTED_OUTPUT:
        raise AssertionError(f"unexpected output.txt\nexpected:\n{EXPECTED_OUTPUT}\nactual:\n{actual}")


def main():
    if sys.argv[1:] == ["setup"]:
        run_setup()
    elif sys.argv[1:] == ["check"]:
        run_check()
    else:
        raise SystemExit("usage: p10_recovery.py setup|check")


if __name__ == "__main__":
    main()
