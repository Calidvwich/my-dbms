#!/usr/bin/env python3

import socket


def connect():
    return socket.create_connection(("127.0.0.1", 8765), timeout=5)


def send_sql(stream, sql):
    stream.sendall(sql.encode("utf-8") + b"\0")
    return stream.recv(8192).rstrip(b"\0").decode("utf-8", errors="replace")


def expect_contains(response, expected, label):
    if expected not in response:
        raise AssertionError(f"{label}: expected {expected!r}, got:\n{response}")


def expect_abort(response, label):
    if response != "abort\n":
        raise AssertionError(f"{label}: expected abort, got:\n{response}")


def setup():
    with connect() as stream:
        for sql in [
            "create table t(id int,score float);",
            "create index t(id);",
            "insert into t values(1,10.0);",
            "insert into t values(2,20.0);",
        ]:
            send_sql(stream, sql)


def main():
    setup()

    with connect() as t1, connect() as t2:
        send_sql(t1, "begin;")
        send_sql(t2, "begin;")
        send_sql(t1, "update t set score = 99.0 where id = 1;")
        expect_abort(send_sql(t2, "select score from t where id = 1;"), "dirty-read no-wait")
        send_sql(t1, "abort;")
        expect_contains(send_sql(t1, "select score from t where id = 1;"), "10.000000", "abort restores update")

    with connect() as t1, connect() as t2:
        send_sql(t1, "begin;")
        send_sql(t2, "begin;")
        send_sql(t1, "update t set score = 11.0 where id = 1;")
        expect_abort(send_sql(t2, "update t set score = 12.0 where id = 1;"), "lost-update no-wait")
        send_sql(t1, "commit;")
        expect_contains(send_sql(t1, "select score from t where id = 1;"), "11.000000", "committed update remains")

    with connect() as t1, connect() as t2:
        send_sql(t1, "begin;")
        first = send_sql(t1, "select score from t where id = 1;")
        expect_contains(first, "11.000000", "repeatable read first read")
        send_sql(t2, "begin;")
        expect_abort(send_sql(t2, "update t set score = 13.0 where id = 1;"), "repeatable-read no-wait")
        second = send_sql(t1, "select score from t where id = 1;")
        expect_contains(second, "11.000000", "repeatable read second read")
        send_sql(t1, "commit;")

    with connect() as t1, connect() as t2:
        send_sql(t1, "begin;")
        expect_contains(send_sql(t1, "select count(*) as cnt from t;"), "2", "phantom first count")
        send_sql(t2, "begin;")
        expect_abort(send_sql(t2, "insert into t values(3,30.0);"), "phantom no-wait")
        expect_contains(send_sql(t1, "select count(*) as cnt from t;"), "2", "phantom second count")
        send_sql(t1, "commit;")

    print("P09 concurrency test passed")


if __name__ == "__main__":
    main()
