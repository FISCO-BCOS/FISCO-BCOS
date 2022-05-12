#!/usr/bin/python
# -*- coding: UTF-8 -*-
import ast
import sys

from common import utilities
from common.utilities import ServiceInfo


class TarsConfig:
    def __init__(self, config):
        self.config = config
        section = "tars"
        self.tars_url = utilities.get_value(
            self.config, section, "tars_url", None, True)
        self.tars_token = utilities.get_value(
            self.config, section, "tars_token", None, True)
        self.tars_pkg_dir = utilities.get_value(
            self.config, section, "tars_pkg_dir", "binary", False)
        if len(self.tars_token) == 0:
            utilities.log_error("Must config 'tars.tars_token'")
            sys.exit(-1)


class ServiceInfoConfig:
    def __init__(self, config, chain_id):
        self.config = config
        self.name = utilities.get_item_value(self.config,
                                             "name", chain_id, False)
        utilities.check_service_name("service_name", self.name)
        self.deploy_ip = utilities.get_item_value(
            self.config, "deploy_ip", None, True)
        self.listen_ip = utilities.get_item_value(
            self.config, "listen_ip", ServiceInfo.default_listen_ip, False)
        self.listen_port = utilities.get_item_value(
            self.config, "listen_port", 20200, False)
        self.thread_count = utilities.get_item_value(
            self.config, "thread_count", 4, False)
        self.expanded_ip = utilities.get_item_value(
            self.config, "expanded_ip", "", False)


class RpcServiceInfo(ServiceInfoConfig):
    def __init__(self, config, chain_id):
        ServiceInfoConfig.__init__(self, config, chain_id)
        self.rpc_service_name = utilities.get_item_value(
            self.config, "rpc_service_name", self.name, False)
        self.gateway_service_name = utilities.get_item_value(
            self.config, "gateway_service_name", None, True)
        utilities.check_service_name(
            "gateway_service_name", self.gateway_service_name)
        self.peers = []


class GatewayServiceInfo(ServiceInfoConfig):
    def __init__(self, config, chain_id):
        ServiceInfoConfig.__init__(self, config, chain_id)
        self.peers = utilities.get_item_value(
            self.config, "peers", [], False)
        self.rpc_service_name = utilities.get_item_value(
            self.config, "rpc_service_name", None, True)
        utilities.check_service_name("rpc_service_name", self.rpc_service_name)
        self.gateway_service_name = utilities.get_item_value(
            self.config, "gateway_service_name", self.name, False)


class NodeConfig:
    def __init__(self, config, group_id, chain_id, index):
        self.config = config
        self.group_id = group_id
        self.chain_id = chain_id
        self.node_count = utilities.get_item_value(
            self.config, "node_count", 1, False)
        self.deploy_ip = utilities.get_item_value(
            self.config, "deploy_ip", None, True)
        self.rpc_service_name = utilities.get_item_value(
            self.config, "rpc_service_name", None, False)
        self.gateway_service_name = utilities.get_item_value(
            self.config, "gateway_service_name", None, False)
        self.obj_name_list = []
        self.node_name = utilities.get_item_value(
            self.config, "node_name", "node" + str(index), False)
        self.expanded_service = utilities.get_item_value(
            self.config, "expanded_service", "", False)
        self.service_list = {}
        self.service_list[ServiceInfo.single_node_service] = self.deploy_ip
        # the obj_name_list
        self.obj_name_list = ServiceInfo.single_node_obj_name_list
        # node_name to node_service_name
        self.nodes_service_name_list = {}
        self.generate_service_name_list()

    def generate_service_name_list(self):
        for i in range(0, self.node_count):
            node_service_info = self.__get_single_node_service_name(i)
            self.nodes_service_name_list[self.__get_node_name(
                i)] = node_service_info

    def __get_node_name(self, index):
        return self.node_name + str(index)

    def __get_service_name(self, index, service):
        return self.group_id + self.__get_node_name(index) + service

    def __get_single_node_service_name(self, index):
        services = {}
        for service in self.service_list.keys():
            services[self.__get_service_name(index, service)] = service
        return services


class GenesisConfig:
    def __init__(self, config):
        self.config = config
        section = "group"
        self.leader_period = utilities.get_value(
            self.config, section, "leader_period", 1, False)
        self.block_tx_count_limit = utilities.get_value(
            self.config, section, "block_tx_count_limit", 1000, False)
        self.consensus_type = utilities.get_value(
            self.config, section, "consensus_type", "pbft", False)
        self.gas_limit = utilities.get_value(
            self.config, section, "gas_limit", "3000000000", False)
        self.compatibility_version = utilities.get_value(
            self.config, section, "compatibility_version", "3.0.0-rc4", False)


class MaxNodeServiceConfig:
    def __init__(self, app_name, base_service_name, service_name, service_obj_list, deploy_ip_list, config_file_list):
        self.app_name = app_name
        self.service_name = service_name
        self.service_obj_list = service_obj_list
        self.deploy_ip_list = deploy_ip_list
        self.base_service_name = base_service_name
        self.config_file_list = config_file_list


class MaxNodeConfig:
    def __init__(self, chain_id, group_id, config):
        self.group_id = group_id
        self.chain_id = chain_id
        self.config = config
        # the node name
        self.node_name = utilities.get_item_value(
            self.config, "node_name", "BcosMaxNode0", False)
        # load the rpc service name
        self.rpc_service_name = utilities.get_item_value(
            self.config, "rpc_service_name", None, True)
        utilities.check_service_name(
            "rpc_service_name", self.rpc_service_name)
        # load the gateway service name
        self.gateway_service_name = utilities.get_item_value(
            self.config, "gateway_service_name", None, True)
        utilities.check_service_name(
            "gateway_service_name", self.gateway_service_name)
        # load the pd_addrs
        self.pd_addrs = utilities.get_item_value(
            self.config, "pd_addrs", None, True)

        self.service_list = []
        # the max_node service config
        max_node_service_name = self.__get_service_name(
            utilities.ServiceInfo.max_node_service)
        max_node_deploy_ip = utilities.get_item_value(
            self.config, "deploy_ip", None, True)
        self.node_config_file_list = [
            "config.ini.tmp", "config.genesis.tmp", "node.pem"]
        self.max_node_service = MaxNodeServiceConfig(self.chain_id, utilities.ServiceInfo.max_node_service, max_node_service_name,
                                                     utilities.ServiceInfo.max_node_service_obj, max_node_deploy_ip, self.node_config_file_list)
        self.service_list.append(self.max_node_service)
        # the executor service config
        executor_service_name = self.__get_service_name(
            utilities.ServiceInfo.executor_service)
        executor_service_deploy_ip = utilities.get_item_value(
            self.config, "executor_deploy_ip", None, True)
        self.executor_config_file_list = ["config.ini.tmp"]
        self.executor_service = MaxNodeServiceConfig(self.chain_id, utilities.ServiceInfo.executor_service, executor_service_name,
                                                     utilities.ServiceInfo.executor_service_obj, executor_service_deploy_ip, self.executor_config_file_list)
        # load scheduler service(for executor)
        self.scheduler_service_name = max_node_service_name
        self.service_list.append(self.executor_service)

    def __get_service_name(self, service_base_name):
        return (self.group_id + self.node_name + service_base_name)


class GroupConfig:
    def __init__(self, config, chain_id):
        self.config = config
        self.chain_id = chain_id
        self.section = "group"
        self.group_id = utilities.get_value(
            self.config, self.section, "group_id", "group", False)
        # check the groupID
        utilities.check_service_name("group_id", self.group_id)
        self.vm_type = utilities.get_value(
            self.config, self.section, "vm_type", "evm", False)
        self.sm_crypto = utilities.get_value(
            self.config, self.section, "sm_crypto", False, False)
        self.auth_check = utilities.get_value(
            self.config, self.section, "auth_check", False, False)
        self.init_auth_address = utilities.get_value(
            self.config, self.section, "init_auth_address", "", self.auth_check)
        self.genesis_config = GenesisConfig(self.config)
        self.node_list = []
        self.__parse_node_config()
        self.max_node_list = []
        self.__parse_max_node_config()

    def __parse_node_config(self):
        node_config_list = utilities.get_value(
            self.config, self.section, "deploy_info", [], False)
        i = 0
        for item in node_config_list:
            self.node_list.append(NodeConfig(
                item, self.group_id, self.chain_id, i))
            i = i + 1

    def __parse_max_node_config(self):
        max_node_list = utilities.get_item_value(
            self.config, "node", [], False)
        for item in max_node_list:
            self.max_node_list.append(MaxNodeConfig(
                self.chain_id, self.group_id, item))


class ChainConfig:
    def __init__(self, config):
        self.config = config
        self.chain_id = utilities.get_value(
            self.config, "chain", "chain_id", "chain", False)
        # check the chain_id
        utilities.check_service_name("chain_id", self.chain_id)
        self.rpc_ca_cert_path = utilities.get_value(
            self.config, "chain", "rpc_ca_cert_path", "", False)
        self.gateway_ca_cert_path = utilities.get_value(
            self.config, "chain", "gateway_ca_cert_path", "", False)
        self.tars_config = TarsConfig(config)
        self.rpc_config = self.__parse_service_config(
            "rpc", self.chain_id, "RpcServiceInfo")
        self.gateway_config = self.__parse_service_config(
            "gateway", self.chain_id, "GatewayServiceInfo")
        self.group_config = GroupConfig(
            config, self.chain_id)
        self.rpc_sm_ssl = utilities.get_value(
            self.config, "chain", "rpc_sm_ssl", False, False)
        self.gateway_sm_ssl = utilities.get_value(
            self.config, "chain", "gateway_sm_ssl", False, False)
        self.enable_failover = utilities.get_value(
            self.config, "chain", "enable_failover", False, False)
        self.failover_cluster_url = utilities.get_value(
            self.config, "chain", "failover_cluster_url", "127.0.0.1:2379", False)

    def __parse_service_config(self, section_name, chain_id, constructor):
        service_list = utilities.get_value(
            self.config, "chain", section_name, [], False)
        result = {}
        for item in service_list:
            service_object = getattr(
                sys.modules[__name__], constructor)(item, chain_id)
            result[service_object.name] = service_object
        return result
