## sudo -H pip install redis
import json
import struct
import sys

import pytest
import redis

from helpers.client import QueryRuntimeException
from helpers.cluster import ClickHouseCluster
from helpers.test_tools import TSV

cluster = ClickHouseCluster(__file__)

node = cluster.add_instance("node", with_redis=True)


@pytest.fixture(scope="module")
def started_cluster():
    try:
        cluster.start()
        yield cluster
    finally:
        cluster.shutdown()


def get_redis_connection(db_id=0):
    client = redis.Redis(
        host="localhost", port=cluster.redis_port, password="clickhouse", db=db_id
    )
    return client


def get_address_for_ch():
    return cluster.redis_host + ":6379"


def drop_table(table):
    node.query(f"DROP TABLE IF EXISTS {table} SYNC")


# see SerializationString.serializeBinary
def serialize_binary_for_string(x):
    var_uint_max = (1 << 63) - 1
    buf = bytearray()
    # write length
    length = len(x)
    # length = (length << 1) ^ (length >> 63)
    if length > var_uint_max:
        raise ValueError("Value too large for varint encoding")
    for i in range(9):
        byte = length & 0x7F
        if length > 0x7F:
            byte |= 0x80
        buf += bytes([byte])
        length >>= 7
        if not length:
            break
    # write data
    buf += x.encode("utf-8")
    return bytes(buf)


# see SerializationNumber.serializeBinary
def serialize_binary_for_uint32(x):
    buf = bytearray()
    packed_num = struct.pack("I", x)
    buf += packed_num
    if sys.byteorder != "little":
        buf.reverse()
    return bytes(buf)


def test_simple_select(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()

    # clean all
    client.flushall()
    drop_table("test_simple_select")

    data = {}
    for i in range(100):
        packed = serialize_binary_for_string(str(i))
        data[packed] = packed

    client.mset(data)
    client.close()

    # create table
    node.query(
        f"""
        CREATE TABLE test_simple_select(
            k String, 
            v String
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    response = TSV.toMat(
        node.query("SELECT k, v FROM test_simple_select WHERE k='0' FORMAT TSV")
    )
    assert len(response) == 1
    assert response[0] == ["0", "0"]

    response = TSV.toMat(
        node.query("SELECT * FROM test_simple_select ORDER BY k FORMAT TSV")
    )
    assert len(response) == 100
    assert response[0] == ["0", "0"]


def test_select_int(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()

    # clean all
    client.flushall()
    drop_table("test_select_int")

    data = {}
    for i in range(100):
        packed = serialize_binary_for_uint32(i)
        data[packed] = packed

    client.mset(data)
    client.close()

    # create table
    node.query(
        f"""
        CREATE TABLE test_select_int(
            k UInt32, 
            v UInt32
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    response = TSV.toMat(
        node.query("SELECT k, v FROM test_select_int WHERE k=0 FORMAT TSV")
    )
    assert len(response) == 1
    assert response[0] == ["0", "0"]

    response = TSV.toMat(
        node.query("SELECT * FROM test_select_int ORDER BY k FORMAT TSV")
    )
    assert len(response) == 100
    assert response[0] == ["0", "0"]


def test_create_table(started_cluster):
    address = get_address_for_ch()

    # simple creation
    drop_table("test_create_table")
    node.query(
        f"""
        CREATE TABLE test_create_table(
            k String,
            v UInt32
        ) Engine=Redis('{address}') PRIMARY KEY (k)
        """
    )

    # simple creation with full engine args
    drop_table("test_create_table")
    node.query(
        f"""
        CREATE TABLE test_create_table(
            k String,
            v UInt32
        ) Engine=Redis('{address}', 0, 'clickhouse', 10) PRIMARY KEY (k)
        """
    )

    drop_table("test_create_table")
    node.query(
        f"""
        CREATE TABLE test_create_table(
            k String,
            f String,
            v UInt32
        ) Engine=Redis('{address}', 0, 'clickhouse', 10) PRIMARY KEY (k)
        """
    )

    drop_table("test_create_table")
    with pytest.raises(QueryRuntimeException):
        node.query(
            f"""
            CREATE TABLE test_create_table(
                k String,
                f String,
                v UInt32
            ) Engine=Redis('{address}', 0, 'clickhouse', 10) PRIMARY KEY ()
            """
        )

    drop_table("test_create_table")
    with pytest.raises(QueryRuntimeException):
        node.query(
            f"""
            CREATE TABLE test_create_table(
                k String,
                f String,
                v UInt32
            ) Engine=Redis('{address}', 0, 'clickhouse', 10)
            """
        )


def test_simple_insert(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()

    # clean all
    client.flushall()
    drop_table("test_simple_insert")

    node.query(
        f"""
        CREATE TABLE test_simple_insert(
            k UInt32, 
            m DateTime,
            n String
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    node.query(
        """
        INSERT INTO test_simple_insert Values 
        (1, '2023-06-01 00:00:00', 'lili'), (2, '2023-06-02 00:00:00', 'lucy')
        """
    )

    response = node.query("SELECT COUNT(*) FROM test_simple_insert FORMAT Values")
    assert response == "(2)"

    response = TSV.toMat(
        node.query("SELECT k, m, n FROM test_simple_insert WHERE k=1 FORMAT TSV")
    )
    assert len(response) == 1
    assert response[0] == ["1", "2023-06-01 00:00:00", "lili"]

    response = TSV.toMat(
        node.query(
            "SELECT k, m, n FROM test_simple_insert WHERE m='2023-06-01 00:00:00' FORMAT TSV"
        )
    )
    assert len(response) == 1
    assert response[0] == ["1", "2023-06-01 00:00:00", "lili"]

    response = TSV.toMat(
        node.query("SELECT k, m, n FROM test_simple_insert WHERE n='lili' FORMAT TSV")
    )
    assert len(response) == 1
    assert response[0] == ["1", "2023-06-01 00:00:00", "lili"]


def test_update(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()
    # clean all
    client.flushall()
    drop_table("test_update")

    node.query(
        f"""
        CREATE TABLE test_update(
            k UInt32, 
            m DateTime,
            n String
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    node.query(
        """
        INSERT INTO test_update Values 
        (1, '2023-06-01 00:00:00', 'lili'), (2, '2023-06-02 00:00:00', 'lucy')
        """
    )

    response = node.query(
        """
        ALTER TABLE test_update UPDATE m='2023-06-03 00:00:00' WHERE k=1
        """
    )

    print("update response: ", response)

    response = TSV.toMat(
        node.query("SELECT k, m, n FROM test_update WHERE k=1 FORMAT TSV")
    )
    assert len(response) == 1
    assert response[0] == ["1", "2023-06-03 00:00:00", "lili"]

    # can not update key
    with pytest.raises(QueryRuntimeException):
        node.query(
            """
            ALTER TABLE test_update UPDATE k=2 WHERE k=1
            """
        )


def test_delete(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()

    # clean all
    client.flushall()
    drop_table("test_delete")

    node.query(
        f"""
        CREATE TABLE test_delete(
            k UInt32, 
            m DateTime,
            n String
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    node.query(
        """
        INSERT INTO test_delete Values 
        (1, '2023-06-01 00:00:00', 'lili'), (2, '2023-06-02 00:00:00', 'lucy')
        """
    )

    response = node.query(
        """
        ALTER TABLE test_delete DELETE WHERE k=1
        """
    )

    print("delete response: ", response)

    response = TSV.toMat(node.query("SELECT k, m, n FROM test_delete FORMAT TSV"))
    assert len(response) == 1
    assert response[0] == ["2", "2023-06-02 00:00:00", "lucy"]

    response = node.query(
        """
        ALTER TABLE test_delete DELETE WHERE m='2023-06-02 00:00:00'
        """
    )

    response = TSV.toMat(node.query("SELECT k, m, n FROM test_delete FORMAT TSV"))
    assert len(response) == 0


def test_truncate(started_cluster):
    client = get_redis_connection()
    address = get_address_for_ch()
    # clean all
    client.flushall()
    drop_table("test_truncate")

    node.query(
        f"""
        CREATE TABLE test_truncate(
            k UInt32, 
            m DateTime,
            n String
        ) Engine=Redis('{address}', 0, 'clickhouse') PRIMARY KEY (k)
        """
    )

    node.query(
        """
        INSERT INTO test_truncate Values 
        (1, '2023-06-01 00:00:00', 'lili'), (2, '2023-06-02 00:00:00', 'lucy')
        """
    )

    response = node.query(
        """
        TRUNCATE TABLE test_truncate
        """
    )

    print("truncate table response: ", response)

    response = TSV.toMat(node.query("SELECT COUNT(*) FROM test_truncate FORMAT TSV"))
    assert len(response) == 1
    assert response[0] == ["0"]


def test_hiding_credentials(started_cluster):
    address = get_address_for_ch()
    table_name = "test_hiding_credentials"
    node.query(
        f"""
        DROP TABLE IF EXISTS {table_name};
        CREATE TABLE {table_name} (k String, v String) Engine=Redis('{address}', 0, "password") PRIMARY KEY (k)
        """
    )
    node.query("SYSTEM FLUSH LOGS")
    message = node.query(f"SELECT message FROM system.text_log WHERE message ILIKE '%CREATE TABLE {table_name}%'")
    assert "password" not in message
    assert f"Redis(\\'{address}\\', 0, \\'[HIDDEN]\\')" in message


def test_direct_join(started_cluster):
    address = get_address_for_ch()

    # clean all
    drop_table("test_direct_join")
    drop_table("test_mt")

    # create table
    node.query(
        f"""
            CREATE TABLE test_direct_join(k Int) Engine=Redis('{address}', 1, 'clickhouse') PRIMARY KEY (k);
            CREATE TABLE test_mt (k Int) ENGINE = MergeTree() ORDER BY tuple();
            INSERT INTO TABLE test_direct_join VALUES (1);
            INSERT INTO TABLE test_mt VALUES (1);
        """
    )

    response = TSV.toMat(node.query("SELECT * FROM test_direct_join JOIN test_mt ON "
                                    "test_direct_join.k = test_mt.k FORMAT TSV"))
    assert len(response) == 1
    assert response[0] == ["1", "1"]

    response = TSV.toMat(node.query("SELECT * FROM test_mt JOIN test_direct_join ON "
                                    "test_direct_join.k = test_mt.k FORMAT TSV"))
    assert len(response) == 1
    assert response[0] == ["1", "1"]


def test_get_keys(started_cluster):
    """
    Checks that ClickHouse reads by key instead of full scan if possible.
    """
    address = get_address_for_ch()

    # clean all
    drop_table("test_get_keys")

    # create table
    node.query(f"""
               CREATE TABLE test_get_keys(k Int) Engine=Redis('{address}', 2, 'clickhouse') PRIMARY KEY (k);
               INSERT INTO test_get_keys VALUES (1), (2), (3);
               """)

    def check_query(query, read_type, keys_count, rows_read):
        plan = node.query(f'EXPLAIN actions=1 {query}')
        assert 'ReadFromRedis' in plan
        assert f'ReadType: {read_type}' in plan
        if read_type == 'GetKeys':
            assert f'Keys: {keys_count}' in plan

        res = node.query(f'{query} FORMAT JSON')
        assert json.loads(res)['statistics']['rows_read'] == rows_read, res

    check_query("SELECT * FROM test_get_keys", "FullScan", 0, 3)
    check_query("SELECT * FROM test_get_keys WHERE k = 1", "GetKeys", 1, 1)
    check_query("SELECT * FROM test_get_keys WHERE k in (3, 5)", "GetKeys", 2, 1)

    plan = node.query("EXPLAIN actions=1, optimize=0 SELECT * FROM test_get_keys")
    assert 'ReadType: FullScan' in plan
