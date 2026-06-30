#!/usr/bin/env python3

import socket
import sys


TEST_GROUPS = {
    "ddl": [
        "create table t1(id int,name char(4));",
        "show tables;",
        "create table t2(id int);",
        "show tables;",
        "drop table t1;",
        "show tables;",
        "drop table t2;",
        "show tables;",
    ],
    "query": [
        "create table grade (name char(4),id int,score float);",
        "insert into grade values ('Data', 1, 90.5);",
        "insert into grade values ('Data', 2, 95.0);",
        "insert into grade values ('Calc', 2, 92.0);",
        "insert into grade values ('Calc', 1, 88.5);",
        "select * from grade;",
        "select score,name,id from grade where score > 90;",
        "select id from grade where name = 'Data';",
        "select name from grade where id = 2 and score > 90;",
    ],
    "update": [
        "update grade set score = 99.0 where name = 'Calc';",
        "select * from grade;",
        "update grade set name = 'test' where name > 'A';",
        "select * from grade;",
        "update grade set name = 'test',id = -1,score = 0 where name = 'test' and score > 90;",
        "select * from grade;",
    ],
    "delete": [
        "delete from grade where score >= 0;",
        "select * from grade;",
        "drop table grade;",
    ],
    "join": [
        "create table t (id int,t_name char(3));",
        "create table d (d_name char(5),id int);",
        "insert into t values (1,'aaa');",
        "insert into t values (2,'baa');",
        "insert into t values (3,'bba');",
        "insert into d values ('12345',1);",
        "insert into d values ('23456',2);",
        "select * from t,d;",
        "select t.id,t_name,d_name from t,d where t.id = d.id;",
    ],
    "invalid": [
        "create table t(id int);",
        "drop table missing_table;",
        "select missing_column from t;",
        "select from t;",
    ],
    "persistence": [
        "show tables;",
        "select * from t;",
        "select * from d;",
    ],
}


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    data = stream.recv(8192)
    return data.rstrip(b"\0").decode("utf-8", errors="replace")


def main():
    groups = sys.argv[1:] or list(TEST_GROUPS)
    with socket.create_connection(("127.0.0.1", 8765), timeout=5) as stream:
        for group in groups:
            for sql in TEST_GROUPS[group]:
                response = send_sql(stream, sql)
                print(f"SQL> {sql}")
                if response:
                    print(response, end="" if response.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
