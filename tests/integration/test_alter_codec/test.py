import pytest
from helpers.client import QueryRuntimeException
from helpers.cluster import ClickHouseCluster


cluster = ClickHouseCluster(__file__)

node1 = cluster.add_instance('node1',
            config_dir='configs',
            main_configs=['configs/logs_config.xml'],
            with_zookeeper=True,
            macros={"shard": 0, "replica": 1} )

node2 = cluster.add_instance('node2',
            config_dir='configs',
            main_configs=['configs/logs_config.xml'],
            with_zookeeper=True,
            macros={"shard": 0, "replica": 2} )


@pytest.fixture(scope="module")
def started_cluster():
    try:
        cluster.start()
        yield cluster

    finally:
        cluster.shutdown()


def test_alter_codec_pk(started_cluster):
    try:
        name = "test_alter_codec_pk"
        node1.query("""
            CREATE TABLE {name} (id UInt64) Engine=MergeTree() ORDER BY id
        """.format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 CODEC(NONE)".format(name=name))
        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt32 CODEC(Delta, LZ4)".format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 DEFAULT 3 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 ALIAS 3 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 MATERIALIZED 3 CODEC(Delta, LZ4)".format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id Int64".format(name=name))

    finally:
        node1.query("DROP TABLE IF EXISTS {name}".format(name=name))


def test_alter_codec_index(started_cluster):
    try:
        name = "test_alter_codec_index"
        node1.query("""
            CREATE TABLE {name} (`id` UInt64, INDEX id_index id TYPE minmax GRANULARITY 1) Engine=MergeTree() ORDER BY tuple()
        """.format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 CODEC(NONE)".format(name=name))
        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt32 CODEC(Delta, LZ4)".format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 DEFAULT 3 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 ALIAS 3 CODEC(Delta, LZ4)".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64 MATERIALIZED 3 CODEC(Delta, LZ4)".format(name=name))

        node1.query("ALTER TABLE {name} MODIFY COLUMN id UInt64".format(name=name))

        with pytest.raises(QueryRuntimeException):
            node1.query("ALTER TABLE {name} MODIFY COLUMN id Int64".format(name=name))

    finally:
        node1.query("DROP TABLE IF EXISTS {name}".format(name=name))
