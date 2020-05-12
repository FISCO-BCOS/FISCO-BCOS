#!/usr/bin/python
# -*- coding: UTF-8 -*-

from common import *
import requests
import time
import json

def constructRequestData(method, params, rpc_id):
    """
    construct data according to given param
    """
    data =json.dumps({"jsonrpc":"2.0","method":method, "params":params, "id":rpc_id})
    return data

def check_diff(status , key, max_diff, stopped_node=65535):
    """
    check diff
    """
    status_value = []
    i = 0
    for single_status in status:
        if(stopped_node != i):
            status_value.append(single_status[key])
        i += 1
    status_value.sort()
    for single_status in status_value:
        if (abs(single_status - status_value[0]) > max_diff):
            return (False, status_value)
    return (True, status_value)

class BasicCheck(object):
    """
    basic check include check sync and check consensus
    """
    def __init__(self, rpc_port=8545, group = 1, node_num = 4, version = "2.4.0", rpc_ip = "127.0.0.1"):
        self.rpc_port = rpc_port
        self.p2p_port = rpc_port + 1000
        self.channel_port = rpc_port + 2000
        self.group_id = group
        self.node_num = node_num
        self.version = version
        self.rpc_ip = rpc_ip

    def build_localdb_blockchain(self):
        """
        build local db blockchain
        """
        LOG_INFO("========== build_localdb_blockchain")
        command = ("bash build_chain.sh -l "+ self.rpc_ip + ":" + str(self.node_num) 
                   + " -e ../build/bin/fisco-bcos -p " + str(self.p2p_port) + "," +
                    str(self.channel_port) + "," + str(self.rpc_port) + " -v " + self.version)
        (status, result) = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + result)
        LOG_INFO("=== start node:")
        command = "nohup bash nodes/" + self.rpc_ip + "/start_all.sh >/dev/null 2>&1 &"
        (status, result) = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + result)
        LOG_INFO("========= build_localdb_blockchain succ")
    
    def stop_all_nodes(self):
        """
        stop all nodes
        """
        command = "bash nodes/" + self.rpc_ip + "/stop_all.sh"
        status, ret = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + ret)
    
    def stop_a_node(self, node_id):
        """
        stop the specified node
        """
        LOG_INFO("========= stop node" + str(node_id))
        command = "bash nodes/" + self.rpc_ip + "/node" + str(node_id) + "/stop.sh"
        status, ret = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + ret)
    
    def start_a_node(self, node_id):
        """
        start the specified node
        """
        LOG_INFO("========= start node" + str(node_id))
        command = "nohup bash ./nodes/" + self.rpc_ip + "/node" + str(node_id) + "/start.sh"
        status, ret = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + ret)

    def stop_and_delete_data(self, node_id):
        """
        stop a node and delete its data
        """
        LOG_INFO("========= stop and delete data of node" + str(node_id))
        self.stop_a_node(node_id)
        command = "rm -rf nodes/" + self.rpc_ip + "/node" + str(node_id) + "/data"
        execute_command(command)

    def send_transaction(self, trans_num):
        """
        send transaction
        """
        LOG_INFO("========== send " + str(trans_num) + " transactions:")
        command = "bash nodes/" + self.rpc_ip + "/.transTest.sh " + str(trans_num) + " " +  str(self.group_id) + " | grep \"jsonrpc\" | grep \"result\" | wc -l"
        (status, result) = execute_command(command)
        LOG_INFO("status = " + str(status) + ", result = " + result)
        LOG_INFO("========== send " + str(trans_num) + " ok")

    def check_all(self, node_id = 0, stopped_node = 65535):
        """
        check consensus and sync
        """
        self.check_consensus(node_id, stopped_node)
        self.check_sync(node_id, stopped_node)

    def check_consensus(self, node_id = 0, stopped_node = 65535):
        """
        callback getConsensusStatus to check status
        """
        LOG_INFO("========== check consensus status for node" + str(node_id))
        param = [self.group_id]
        method = "getConsensusStatus"
        rpc_id = 1
        try:
            request_data = constructRequestData(method, param, rpc_id)
            url = "http://" + self.rpc_ip + ":" + str(self.rpc_port + node_id)
            response = requests.post(url, data = request_data)
            if (response.status_code == requests.codes.ok):
                json_object = json.loads(response.text)
                if("result" in json_object):
                    view_status = json_object["result"][1]
                    max_diff = 2*self.node_num + 1
                    (ret, view_status) = check_diff(view_status, "view", max_diff, stopped_node)
                    if(ret is False):
                        LOG_ERROR("check consensus failed, current views:" + str(view_status))
                    LOG_INFO("check consensus succ")
                else:
                    LOG_ERROR("unexpected return value of getConsensusStatus:" + response.text)
            else:
                LOG_ERROR("getConsensusStatus for node" + str(node_id) + " failed, maybe the node has been shut-down!")
        except Exception as err:
            LOG_ERROR("getConsensusStatus for node" + str(node_id) + " failed, error info:" + str(err))

    def getTotalTransaction(self, node_id):
        """
        get block number
        """
        param = [self.group_id]
        method = "getTotalTransactionCount"
        rpc_id = 1
        try:
            request_data = constructRequestData(method, param, rpc_id)
            url = "http://" + self.rpc_ip + ":" + str(self.rpc_port + node_id)
            response = requests.post(url, data = request_data)
            if(response.status_code == requests.codes.ok):
                json_object = json.loads(response.text)
                if("result" in json_object):
                    block_number = json_object["result"]["blockNumber"]
                    txSum = json_object["result"]["txSum"]
                else:
                    LOG_ERROR("unexpected return value of getBlockNumber:" + response.text)
            else:
                LOG_ERROR("getBlockNumber for node" + str(node_id) + " failed, maybe the node has been shut-down!")
            return int(block_number, 16), int(txSum, 16)
        except Exception, e:
            LOG_ERROR("getBlockNumber for node" + str(node_id) + " failed, maybe the node has been shut-down, error info:" + str(e))



    def check_sendTransaction(self, node_id, expectedIncreNumber):
        """
        check block number
        """
        cur_block_number, cur_txs = self.getTotalTransaction(node_id)
        LOG_INFO("========== current block number of node" + str(node_id) + ": " + str(cur_block_number) + ", txSum:" + str(cur_txs))
        # send transactions
        self.send_transaction(expectedIncreNumber)
        if(expectedIncreNumber > 5):
            time.sleep(2 + expectedIncreNumber/5)
        else:
            time.sleep(2)
        (updated_block_number, updated_txs) = self.getTotalTransaction(node_id)
        LOG_INFO("========== updated block number of node" + str(node_id) + ": " + str(updated_block_number) + ", txSum:" + str(updated_txs))
        if(updated_txs - cur_txs != expectedIncreNumber):
            LOG_ERROR("check sendTransactions from node" + str(node_id) + " failed!")
        else:
            LOG_INFO("check sendTransactions from node" + str(node_id) + " succ!")


    def check_sync(self, node_id = 0, stopped_node=65535):
        """
        callback getSyncStatus to check status
        """
        LOG_INFO("========== check sync status")
        param = [self.group_id]
        method = "getSyncStatus"
        rpc_id = 1
        try:
            request_data = constructRequestData(method, param, rpc_id)
            url = "http://" + self.rpc_ip + ":" + str(self.rpc_port + node_id)
            response = requests.post(url, data = request_data)
            if (response.status_code == requests.codes.ok):
                json_object = json.loads(response.text)
                if("result" in json_object):
                    peers = json_object["result"]["peers"]
                    (ret, block_number_status) = check_diff(peers, "blockNumber", 0, stopped_node)
                    if(ret is False):
                        LOG_ERROR("check sync failed, blockNumber status:".join(block_number_status))
                    LOG_INFO("check sync succ")
                else:
                    LOG_ERROR("unmatch return value of getSyncStatus")
            else:
                LOG_ERROR("getSyncStatus failed, maybe the node has been shut-down!")
        except Exception, e:
            LOG_ERROR("getSyncStatus failed, error information:" + str(e))
