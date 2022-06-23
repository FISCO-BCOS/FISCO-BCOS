#!/usr/bin/python
# -*- coding: UTF-8 -*-
import sys
import os

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


class GenesisConfig:
    def __init__(self, config):
        self.config = config
        section = "group"
        self.desc = "[[group]]"
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
        self.vm_type = utilities.get_item_value(
            self.config, "vm_type", "evm", False, self.desc)
        self.auth_check = utilities.get_item_value(
            self.config, "auth_check", False, False, self.desc)
        self.init_auth_address = utilities.get_item_value(
            self.config, "init_auth_address", "", self.auth_check, self.desc)



class AgencyConfig:
    def __init__(self, config, chain_id, default_url, enforce_failover):
        """
        init the agencyConfig
        """
        self.config = config
        self.chain_id = chain_id
        # the agencyName
        self.desc = "[[agency]]."
        self.name = utilities.get_item_value(
            self.config, "name", None, True, self.desc)
        utilities.check_service_name("agency.name", self.name)
        # the rpc service_name
        self.rpc_service_name = self.name + utilities.ServiceInfo.rpc_service
        # the gateway service_name
        self.gateway_service_name = self.name + utilities.ServiceInfo.gateway_service
        # the failover cluster url
        self.failover_cluster_url = utilities.get_item_value(
            self.config, "failover_cluster_url", default_url, enforce_failover, self.desc)
        # load storage_security config
        self.enable_storage_security = utilities.get_item_value(
            self.config, "enable_storage_security", False, False, self.desc)
        self.sm_storage_security = utilities.get_item_value(
            self.config, "sm_storage_security", False, False, self.desc)
        self.key_center_url = utilities.get_item_value(
            self.config, "key_center_url", "", False, self.desc)
        self.cipher_data_key = utilities.get_item_value(
            self.config, "cipher_data_key", "", False, self.desc)


class ServiceInfoConfig:
    def __init__(self, config, agency_config, service_obj_list, name, service_type, tpl_config_file, ca_cert_path, sm_ssl, binary_name):
        self.config = config
        self.name = name
        self.ca_cert_path = ca_cert_path
        self.service_type = service_type
        self.tpl_config_file = tpl_config_file
        self.agency_config = agency_config
        self.service_obj_list = service_obj_list
        self.sm_ssl = sm_ssl
        self.binary_name = binary_name
        # the service deploy ip
        self.desc = "[agency." + service_type + "]."
        self.deploy_ip_list = utilities.get_item_value(
            self.config, "deploy_ip", None, True, self.desc)
        self.listen_ip = utilities.get_item_value(
            self.config, "listen_ip", ServiceInfo.default_listen_ip, False, self.desc)
        self.listen_port = utilities.get_item_value(
            self.config, "listen_port", 20200, False, self.desc)
        self.thread_count = utilities.get_item_value(
            self.config, "thread_count", 4, False, self.desc)
        # self.expanded_ip = utilities.get_item_value(
        #    self.config, "expanded_ip", "", False)
        # peers info
        self.peers = utilities.get_item_value(
            self.config, "peers", [], False, self.desc)


class NodeServiceConfig:
    def __init__(self, app_name, base_service_name, service_name, service_obj_list, deploy_ip_list, config_file_list):
        self.app_name = app_name
        self.service_name = service_name
        self.service_obj_list = service_obj_list
        self.deploy_ip_list = deploy_ip_list
        self.base_service_name = base_service_name
        self.config_file_list = config_file_list


class NodeConfig:
    def __init__(self, config, chain_id, group_id, agency_config, node_service_base_name, node_service_obj_list, sm_crypto, node_type):
        self.chain_id = chain_id
        self.group_id = group_id
        self.agency_config = agency_config
        self.config = config
        # the node name
        self.desc = "[[agency.group.node]]."
        self.node_name = utilities.get_item_value(
            self.config, "node_name", None, True, self.desc)
        # load storage_security
        self.enable_storage_security = utilities.get_item_value(
            self.config, "enable_storage_security", False, False, self.desc)
        self.key_center_url = utilities.get_item_value(
            self.config, "key_center_url", "", False, self.desc)
        self.cipher_data_key = utilities.get_item_value(
            self.config, "cipher_data_key", "", False, self.desc)
        self.deploy_ip = utilities.get_item_value(
            self.config, "deploy_ip", None, True, self.desc)
        self.monitor_listen_port = utilities.get_item_value(
            self.config, "monitor_listen_port", None, True, self.desc)
        self.monitor_log_path = utilities.get_item_value(
            self.config, "monitor_log_path", None, True, self.desc)     
        # parse node_service_config
        self.node_service_base_name = node_service_base_name
        self.node_service_obj_list = node_service_obj_list
        self.sm_crypto = sm_crypto
        self.service_list = []
        self.key_page_size = 0
        self.__parse_node_service_config(node_type)

    def __parse_node_service_config(self, node_type):
        """
        parse and load the node_service config
        """
        # the max_node service config
        self.node_service_name = self.get_service_name(
            self.node_service_base_name)
        node_deploy_ip = utilities.get_item_value(
            self.config, "deploy_ip", None, True, self.desc)
        self.node_config_file_list = [
            "config.ini", "config.genesis", "node.pem"]
        deploy_ip_list = []
        if node_type != "max":
            deploy_ip_list.append(node_deploy_ip)
        else:
            deploy_ip_list = node_deploy_ip
        self.node_service = NodeServiceConfig(self.agency_config.chain_id, self.node_service_base_name, self.node_service_name,
                                              self.node_service_obj_list, deploy_ip_list, self.node_config_file_list)
        self.service_list.append(self.node_service)

    def get_service_name(self, service_base_name):
        return (self.agency_config.name + self.group_id + self.node_name + service_base_name)


class ProNodeConfig(NodeConfig):
    """
    the pro-node config
    """

    def __init__(self, config, chain_id, group_id, agency_config, sm_crypto):
        NodeConfig.__init__(self, config, chain_id, group_id, agency_config,
                            utilities.ServiceInfo.single_node_service, utilities.ServiceInfo.single_node_obj_name_list, sm_crypto, "pro")


class MaxNodeConfig(NodeConfig):
    def __init__(self, config, chain_id, group_id, agency_config, sm_crypto):
        """
        the max-node config
        """
        NodeConfig.__init__(self, config, chain_id, group_id, agency_config,
                            utilities.ServiceInfo.max_node_service, utilities.ServiceInfo.single_node_obj_name_list, sm_crypto, "max")
        # load the pd_addrs
        self.pd_addrs = utilities.get_item_value(
            self.config, "pd_addrs", None, True, self.desc)
        # the executor service config
        self.__parse_executor_service_config()
        # load service name(for executor)
        self.__load_service_name()

    def __parse_executor_service_config(self):
        """
        parse and load the executor service_config
        """
        executor_service_name = self.get_service_name(
            utilities.ServiceInfo.executor_service)
        executor_service_deploy_ip = utilities.get_item_value(
            self.config, "executor_deploy_ip", None, True, self.desc)
        self.executor_config_file_list = ["config.ini", "config.genesis"]
        self.executor_service = NodeServiceConfig(self.chain_id, utilities.ServiceInfo.executor_service, executor_service_name,
                                                  utilities.ServiceInfo.executor_service_obj, executor_service_deploy_ip, self.executor_config_file_list)
        self.service_list.append(self.executor_service)

    def __load_service_name(self):
        self.scheduler_service_name = self.node_service_name
        self.txpool_service_name = self.node_service_name


class GroupConfig:
    def __init__(self, config, chain_id):
        self.config = config
        self.chain_id = chain_id
        self.desc = "[[group]]."
        self.group_id = utilities.get_item_value(
            self.config, "group_id", "group", False, self.desc)
        # check the groupID
        utilities.check_service_name("group_id", self.group_id)
        default_genesis_config_path = os.path.join(
            "generated/", self.chain_id, self.group_id, "config.genesis")
        self.genesis_config_path = utilities.get_item_value(
            self.config, "genesis_config_path", default_genesis_config_path, False, self.desc)
        # self.vm_type = utilities.get_item_value(
        #     self.config, "vm_type", "evm", False, self.desc)
        self.sm_crypto = utilities.get_item_value(
            self.config, "sm_crypto", False, False, self.desc)
        # self.auth_check = utilities.get_item_value(
        #     self.config, "auth_check", False, False, self.desc)
        # self.init_auth_address = utilities.get_item_value(
        #     self.config, "init_auth_address", "", self.auth_check, self.desc)
        self.genesis_config = GenesisConfig(self.config)
        self.node_list = []

    def append_node_list(self, node_list):
        self.node_list = self.node_list + node_list


class ChainConfig:
    def __init__(self, config, node_type, should_load_node_config):
        self.config = config
        self.node_type = node_type
        self.enforce_failover = False
        if self.node_type == "max":
            self.enforce_failover = True
        self.default_failover_url = "127.0.0.1:2379"
        self.tars_config = TarsConfig(config)
        self.desc = "[chain]."
        self.__load_chain_config()
        # load the group list
        self.group_list = {}
        self.__load_group_list()
        # agency_name to agency_config
        self.agency_list = {}
        # rpc_service_name to rpc_service
        self.rpc_service_list = {}
        # gateway_service_name to gateway_service
        self.gateway_service_list = {}
        # the node list
        self.node_list = {}
        self.__load_agency_config(should_load_node_config)

    def __load_group_list(self):
        """
        load the group list
        """
        group_list_config = utilities.get_item_value(
            self.config, "group", [], False, self.desc)
        for group in group_list_config:
            group_config = GroupConfig(group, self.chain_id)
            self.group_list[group_config.group_id] = group_config

    def __load_chain_config(self):
        """
        load the chain_config
        """
        self.chain_id = utilities.get_value(
            self.config, "chain", "chain_id", "chain", False)
        # check the chain_id
        utilities.check_service_name("chain_id", self.chain_id)
        default_rpc_ca_cert = os.path.join(
            "./generated/rpc", self.chain_id, "ca")
        self.rpc_ca_cert_path = utilities.get_value(
            self.config, "chain", "rpc_ca_cert_path", default_rpc_ca_cert, False)
        default_gateway_ca_cert = os.path.join(
            "./generated/gateway", self.chain_id, "ca")
        self.gateway_ca_cert_path = utilities.get_value(
            self.config, "chain", "gateway_ca_cert_path", default_gateway_ca_cert, False)
        self.rpc_sm_ssl = utilities.get_value(
            self.config, "chain", "rpc_sm_ssl", False, False)
        self.gateway_sm_ssl = utilities.get_value(
            self.config, "chain", "gateway_sm_ssl", False, False)

    def __load_agency_config(self, should_load_node_config):
        """
        load the agency config
        """
        agency_list = utilities.get_item_value(
            self.config, "agency", [], False, "")
        for agency in agency_list:
            # parse the agency config
            agency_config = AgencyConfig(
                agency, self.chain_id, self.default_failover_url, self.enforce_failover)
            self.agency_list[agency_config.name] = agency_config
            # parse the rpc service config
            rpc_config_section = utilities.get_item_value(
                agency, "rpc", None, False, "[agency.rpc]")
            if rpc_config_section is not None:
                rpc_config = ServiceInfoConfig(rpc_config_section, agency_config, utilities.ServiceInfo.rpc_service_obj, agency_config.rpc_service_name,
                                               utilities.ServiceInfo.rpc_service_type, utilities.ConfigInfo.rpc_config_tpl_path,  self.rpc_ca_cert_path, self.rpc_sm_ssl, utilities.ServiceInfo.rpc_service)
                self.rpc_service_list[rpc_config.name] = rpc_config
            # parse the gateway service config
            gateway_config_section = utilities.get_item_value(
                agency, "gateway", None, False, "[agency.gateway]")
            if gateway_config_section is not None:
                gateway_config = ServiceInfoConfig(gateway_config_section, agency_config, utilities.ServiceInfo.gateway_service_obj, agency_config.gateway_service_name,
                                                   utilities.ServiceInfo.gateway_service_type, utilities.ConfigInfo.gateway_config_tpl_path,  self.gateway_ca_cert_path, self.gateway_sm_ssl, utilities.ServiceInfo.gateway_service)
                self.gateway_service_list[gateway_config.name] = gateway_config
            # parse the node config
            if should_load_node_config is True:
                self.__load_node_config(agency, agency_config)

    def __load_node_config(self, agency_config_section, agency_config):
        agency_group_list = utilities.get_item_value(
            agency_config_section, "group", [], False, "[[agency.group]]")
        for group_config in agency_group_list:
            group_id = utilities.get_item_value(
                group_config, "group_id", None, True, "[[agency.group]]")
            if group_id not in self.group_list.keys():
                utilities.log_error(
                    "Load node config failed for the group %s configuration has not been setted." % group_id)
                sys.exit(-1)
            group_config_obj = self.group_list.get(group_id)
            node_config = utilities.get_item_value(
                group_config, "node", None, False, "[[agency.group.node]]")
            group_node_list = []
            for node in node_config:
                node_service = None
                if self.node_type == "max":
                    node_service = MaxNodeConfig(
                        node, self.chain_id, group_id, agency_config, group_config_obj.sm_crypto)
                else:
                    node_service = ProNodeConfig(
                        node, self.chain_id, group_id, agency_config, group_config_obj.sm_crypto)
                self.node_list[node_service.node_service_name] = node_service
                group_node_list.append(node_service)
            self.group_list[group_id].append_node_list(group_node_list)
