import time

import pytest

import helpers.keeper_utils as keeper_utils
from helpers.cluster import ClickHouseCluster

cluster = ClickHouseCluster(__file__)
node1 = cluster.add_instance(
    "node1", main_configs=["configs/keeper_config_with_allow_list.xml"], stay_alive=True
)
node2 = cluster.add_instance(
    "node2",
    main_configs=["configs/keeper_config_without_allow_list.xml"],
    stay_alive=True,
)
node3 = cluster.add_instance(
    "node3",
    main_configs=["configs/keeper_config_with_allow_list_all.xml"],
    stay_alive=True,
)


@pytest.fixture(scope="module")
def started_cluster():
    try:
        cluster.start()

        yield cluster

    finally:
        cluster.shutdown()


def destroy_zk_client(zk):
    try:
        if zk:
            zk.stop()
            zk.close()
    except:
        pass


def wait_node(node):
    for _ in range(100):
        zk = None
        try:
            zk = get_fake_zk(node.name, timeout=30.0)
            # zk.create("/test", sequence=True)
            print("node", node.name, "ready")
            break
        except Exception as ex:
            time.sleep(0.2)
            print("Waiting until", node.name, "will be ready, exception", ex)
        finally:
            destroy_zk_client(zk)
    else:
        raise Exception("Can't wait node", node.name, "to become ready")


def wait_nodes():
    for n in [node1, node2, node3]:
        wait_node(n)


def get_keeper_socket(nodename):
    return keeper_utils.get_keeper_socket(cluster, nodename)


def get_fake_zk(nodename, timeout=30.0):
    return keeper_utils.get_fake_zk(cluster, nodename, timeout=timeout)


def close_keeper_socket(cli):
    if cli is not None:
        print("close socket")
        cli.close()


def send_cmd(node_name, command="ruok"):
    client = None
    try:
        wait_nodes()
        client = get_keeper_socket(node_name)
        client.send(command.encode())
        data = client.recv(4)
        return data.decode()
    finally:
        close_keeper_socket(client)


def test_allow_list(started_cluster):
    wait_nodes()
    assert send_cmd(node1.name) == "imok"
    assert send_cmd(node1.name, command="mntr") == ""
    assert send_cmd(node2.name) == "imok"
    assert send_cmd(node3.name) == "imok"
