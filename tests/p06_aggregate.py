#!/usr/bin/env python3

import socket


COMMANDS = [
    "create table aggregate(id int,val float,name char(4));",
    "insert into aggregate values(1,1.5,'aaaa');",
    "insert into aggregate values(2,2.0,'cccc');",
    "insert into aggregate values(3,2.0,'bbbb');",
    "select sum(id) as sum_id from aggregate;",
    "select sum(val) as sum_val from aggregate;",
    "select max(id) as max_id,min(id) as min_id from aggregate;",
    "select max(val) as max_val,min(val) as min_val from aggregate;",
    "select max(name) as max_name,min(name) as min_name from aggregate;",
    "select count(*) as count_row,count(name) as count_name from aggregate;",
    "select count(name) as count_name from aggregate where val = 2.0;",
    "select count(*) as empty_count from aggregate where id > 100;",
    "create index aggregate(id);",
    "select sum(id) as indexed_sum from aggregate where id >= 2;",
    "select sum(name) as invalid_sum from aggregate;",
    "select max(id) as missing_alias from missing_table;",
]


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


if __name__ == "__main__":
    main()
