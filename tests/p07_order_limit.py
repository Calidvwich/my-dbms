#!/usr/bin/env python3

import socket


COMMANDS = [
    "create table orders(company char(4),order_number int,amount float);",
    "insert into orders values('beta',3,30.0);",
    "insert into orders values('acme',2,20.0);",
    "insert into orders values('beta',1,10.0);",
    "insert into orders values('acme',4,40.0);",
    "select company,order_number from orders order by order_number;",
    "select company,order_number from orders order by order_number desc;",
    "select company,order_number from orders order by company desc,order_number asc;",
    "select company from orders order by amount desc limit 2;",
    "select company,order_number from orders order by order_number asc limit 2;",
    "select company,order_number from orders limit 2;",
    "select company,order_number from orders order by order_number limit 0;",
    "select company from orders order by missing;",
    "select company from orders limit -1;",
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
