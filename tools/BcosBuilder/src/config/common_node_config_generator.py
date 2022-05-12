#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
from common import utilities
from common.utilities import ConfigInfo
import uuid
import os


class CommonConfigGenerator:
    """
    the common node config generator
    """

    def __init__(self, config):
        self.config = config
        self.genesis_tpl_config = ConfigInfo.genesis_config_tpl_path
        self.node_tpl_config = ConfigInfo.node_config_tpl_path
        self.executor_tpl_config = ConfigInfo.executor_config_tpl_path
        self.node_pem_file = "node.pem"
        self.node_id_file = "node.nodeid"

    def generate_genesis_config(self, nodeid_list):
        """
        generate the gensis config
        """
        utilities.log_info("* generate_genesis_config")
        config_content = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        config_content.read(self.genesis_tpl_config)
        consensus_section = "consensus"
        config_content[consensus_section]["consensus_type"] = self.config.group_config.genesis_config.consensus_type
        config_content[consensus_section]["block_tx_count_limit"] = str(
            self.config.group_config.genesis_config.block_tx_count_limit)
        config_content[consensus_section]["leader_period"] = str(
            self.config.group_config.genesis_config.leader_period)
        i = 0
        for nodeid in nodeid_list:
            key = "node." + str(i)
            value = nodeid.strip() + ":1"
            config_content[consensus_section][key] = value
            i = i + 1
        tx_section = "tx"
        config_content[tx_section]["gas_limit"] = str(
            self.config.group_config.genesis_config.gas_limit)
        version_section = "version"
        config_content[version_section]["compatibility_version"] = str(
            self.config.group_config.genesis_config.compatibility_version)
        utilities.log_info("* consensus_type: %s" %
                           config_content[consensus_section]["consensus_type"])
        utilities.log_info("* block_tx_count_limit: %s" %
                           config_content[consensus_section]["block_tx_count_limit"])
        utilities.log_info("* leader_period: %s" %
                           config_content[consensus_section]["leader_period"])
        utilities.log_info("* gas_limit: %s" %
                           config_content[tx_section]["gas_limit"])
        utilities.log_info("* compatibility_version: %s" %
                           config_content[version_section]["compatibility_version"])
        utilities.log_info("* generate_genesis_config success")
        return config_content

    def generate_executor_config(self, node_config, node_name):
        """
        generate the config.ini for executorService
        """
        executor_ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        executor_ini_config.read(self.executor_tpl_config)
        # chain config
        self.__update_chain_info(executor_ini_config)
        # service config
        service_section = "service"
        executor_ini_config[service_section]["node_name"] = node_name
        executor_ini_config[service_section]["scheduler"] = self.config.chain_id + \
            "." + node_config.scheduler_service_name
        executor_ini_config[service_section]["txpool"] = self.config.chain_id + \
            "." + node_config.txpool_service_name
        # executor config
        self.__update_executor_info(executor_ini_config)
        self.__update_storage_info(executor_ini_config, node_config, "max")
        return executor_ini_config

    def generate_node_config(self, node_config, node_name, node_type):
        """
        generate node config: config.ini.tmp
        """
        ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        ini_config.read(self.node_tpl_config)
        self.__update_chain_info(ini_config)
        self.__update_service_info(ini_config, node_config, node_name)
        self.__update_failover_info(ini_config, node_config, node_type)
        self.__update_executor_info(ini_config)
        # set storage config
        self.__update_storage_info(ini_config, node_config, node_type)
        return ini_config

    def __update_chain_info(self, ini_config):
        """
        update chain info
        """
        chain_section = "chain"
        ini_config[chain_section]["sm_crypto"] = utilities.convert_bool_to_str(
            self.config.group_config.sm_crypto)
        ini_config[chain_section]["group_id"] = self.config.group_config.group_id
        ini_config[chain_section]["chain_id"] = self.config.group_config.chain_id

    def __update_service_info(self, ini_config, node_config, node_name):
        """
        update service info
        """
        service_section = "service"
        ini_config[service_section]["node_name"] = node_name
        ini_config[service_section]["rpc"] = self.config.chain_id + \
            "." + node_config.rpc_service_name
        ini_config[service_section]["gateway"] = self.config.chain_id + \
            "." + node_config.gateway_service_name

    def __update_failover_info(self, ini_config, node_config, node_type):
        # generate the member_id for failover
        failover_section = "failover"
        ini_config[failover_section]["member_id"] = str(uuid.uuid1())
        if node_type == "max":
            ini_config[failover_section]["enable"] = utilities.convert_bool_to_str(
                True)
            ini_config[failover_section]["cluster_url"] = node_config.pd_addrs
        else:
            ini_config[failover_section]["enable"] = utilities.convert_bool_to_str(
                False)

    def __update_executor_info(self, ini_config):
        executor_section = "executor"
        if self.config.group_config.vm_type == "evm":
            ini_config[executor_section]["is_wasm"] = utilities.convert_bool_to_str(
                False)
        if self.config.group_config.vm_type == "wasm":
            ini_config[executor_section]["is_wasm"] = utilities.convert_bool_to_str(
                True)
        ini_config[executor_section]["is_auth_check"] = utilities.convert_bool_to_str(
            self.config.group_config.auth_check)
        ini_config[executor_section]["auth_admin_account"] = self.config.group_config.init_auth_address

    def __update_storage_info(self, ini_config, node_config, node_type):
        if node_type != "max":
            return
        storage_section = "storage"
        if ini_config.has_option(storage_section, "data_path"):
            ini_config.remove_option(storage_section, "data_path")
        ini_config[storage_section]["type"] = "tikv"
        ini_config[storage_section]["pd_addrs"] = node_config.pd_addrs

    def generate_pem_file(self, outputdir):
        """
        generate private key to the given path
        """
        pem_path = os.path.join(outputdir, self.node_pem_file)
        node_id_path = os.path.join(outputdir, self.node_id_file)
        utilities.generate_private_key(
            self.config.group_config.sm_crypto, outputdir)
        node_id = ""
        with open(node_id_path, 'r', encoding='utf-8') as f:
            node_id = f.read()
        return (node_id, pem_path, node_id_path)
