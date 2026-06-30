#!/usr/bin/env python3

import socket


COMMANDS = [
    "create table account(id int,balance float,created datetime);",
    "create index account(id);",
    "insert into account values(1,10.0,'2024-01-01 00:00:00');",
    "begin;",
    "insert into account values(2,20.0,'2024-01-02 00:00:00');",
    "update account set balance = 99.5 where id = 1;",
    "delete from account where id = 2;",
    "abort;",
    "select id,balance,created from account order by id;",
    "begin;",
    "insert into account values(2,20.0,'2024-01-02 00:00:00');",
    "update account set balance = 88.0 where id = 1;",
    "commit;",
    "select id,balance,created from account order by id;",
    "begin;",
    "delete from account where id = 1;",
    "update account set created = '2024-02-01 12:30:00' where id = 2;",
    "abort;",
    "select id,balance,created from account order by id;",
]

EXPECTED_OUTPUT = """| id | balance | created |
| 1 | 10.000000 | 2024-01-01 00:00:00 |
| id | balance | created |
| 1 | 88.000000 | 2024-01-01 00:00:00 |
| 2 | 20.000000 | 2024-01-02 00:00:00 |
| id | balance | created |
| 1 | 88.000000 | 2024-01-01 00:00:00 |
| 2 | 20.000000 | 2024-01-02 00:00:00 |
"""


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    return stream.recv(8192).rstrip(b"\0").decode("utf-8", errors="replace")


def main():
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        for sql in COMMANDS:
            response = send_sql(stream, sql)
            print(f"SQL> {sql}")
            if response:
                print(response, end="" if response.endswith("\n") else "\n")

    with open("build/p08_transaction_db/output.txt", "r", encoding="utf-8") as output_file:
        actual = output_file.read()
    if actual != EXPECTED_OUTPUT:
        raise AssertionError(f"unexpected output.txt\nexpected:\n{EXPECTED_OUTPUT}\nactual:\n{actual}")


if __name__ == "__main__":
    main()
