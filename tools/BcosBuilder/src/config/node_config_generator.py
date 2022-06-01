#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
from common import utilities
from common.utilities import ConfigInfo
import uuid
import os
import sys


class NodeConfigGenerator:
    """
    the common node config generator
    """

    def __init__(self, config, node_type):
        self.config = config
        self.genesis_tpl_config = ConfigInfo.genesis_config_tpl_path
        self.node_tpl_config = ConfigInfo.node_config_tpl_path
        self.executor_tpl_config = ConfigInfo.executor_config_tpl_path
        self.node_pem_file = "node.pem"
        self.node_id_file = "node.nodeid"
        self.root_dir = "./generated"
        self.genesis_tmp_config_file = 'config.genesis'
        self.ini_tmp_config_file = "config.ini"
        self.node_type = node_type

    def generate_genesis_config_nodeid(self, nodeid_list, group_config):
        """
        generate the gensis config
        """
        utilities.log_info("* generate_genesis_config_nodeid")
        config_content = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        config_content.read(self.genesis_tpl_config)
        consensus_section = "consensus"
        config_content[consensus_section]["consensus_type"] = group_config.genesis_config.consensus_type
        config_content[consensus_section]["block_tx_count_limit"] = str(
            group_config.genesis_config.block_tx_count_limit)
        config_content[consensus_section]["leader_period"] = str(
            group_config.genesis_config.leader_period)
        i = 0
        for nodeid in nodeid_list:
            key = "node." + str(i)
            value = nodeid.strip() + ":1"
            config_content[consensus_section][key] = value
            i = i + 1
        tx_section = "tx"
        config_content[tx_section]["gas_limit"] = str(
            group_config.genesis_config.gas_limit)
        version_section = "version"
        config_content[version_section]["compatibility_version"] = str(
            group_config.genesis_config.compatibility_version)

        executor_section = "executor"
        config_content[executor_section]["is_wasm"] = utilities.convert_bool_to_str(
            group_config.genesis_config.vm_type != "evm")
        config_content[executor_section]["is_auth_check"] = utilities.convert_bool_to_str(
            group_config.genesis_config.auth_check)
        config_content[executor_section]["auth_admin_account"] = group_config.genesis_config.init_auth_address

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
        utilities.log_info("* generate_genesis_config_nodeid success")
        return config_content

    def generate_executor_config(self, group_config, node_config, node_name):
        """
        generate the config.ini for executorService
        """
        executor_ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        executor_ini_config.read(self.executor_tpl_config)
        # chain config
        self.__update_chain_info(executor_ini_config, node_config)
        # service config
        service_section = "service"
        executor_ini_config[service_section]["node_name"] = node_name
        executor_ini_config[service_section]["scheduler"] = self.config.chain_id + \
            "." + node_config.scheduler_service_name
        executor_ini_config[service_section]["txpool"] = self.config.chain_id + \
            "." + node_config.txpool_service_name
        # executor config
        self.__update_storage_info(
            executor_ini_config, node_config, self.node_type)
        return executor_ini_config

    def __generate_node_config(self, group_config, node_config, node_name, node_type):
        """
        generate node config: config.ini.tmp
        """
        ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        ini_config.read(self.node_tpl_config)
        self.__update_chain_info(ini_config, node_config)
        self.__update_service_info(ini_config, node_config, node_name)
        self.__update_failover_info(ini_config, node_config, node_type)
        # set storage config
        self.__update_storage_info(ini_config, node_config, node_type)
        # set storage_security config
        # TODO: access key_center to encrypt the certificates and the private keys
        self.__update_storage_security_info(ini_config, node_config, node_type)
        return ini_config

    def __update_chain_info(self, ini_config, node_config):
        """
        update chain info
        """
        chain_section = "chain"
        ini_config[chain_section]["sm_crypto"] = utilities.convert_bool_to_str(
            node_config.sm_crypto)
        ini_config[chain_section]["group_id"] = node_config.group_id
        ini_config[chain_section]["chain_id"] = node_config.chain_id

    def __update_service_info(self, ini_config, node_config, node_name):
        """
        update service info
        """
        service_section = "service"
        ini_config[service_section]["node_name"] = node_name
        ini_config[service_section]["rpc"] = self.config.chain_id + \
            "." + node_config.agency_config.rpc_service_name
        ini_config[service_section]["gateway"] = self.config.chain_id + \
            "." + node_config.agency_config.gateway_service_name
        ini_config[service_section]["executor"] = self.config.chain_id + \
            "." + node_config.node_service.service_name

    def __update_failover_info(self, ini_config, node_config, node_type):
        # generate the member_id for failover
        failover_section = "failover"
        ini_config[failover_section]["member_id"] = str(uuid.uuid1())
        if node_type == "max":
            ini_config[failover_section]["enable"] = utilities.convert_bool_to_str(
                True)
            ini_config[failover_section]["cluster_url"] = node_config.agency_config.failover_cluster_url
        else:
            ini_config[failover_section]["enable"] = utilities.convert_bool_to_str(
                False)

    def __update_storage_info(self, ini_config, node_config, node_type):
        if node_type != "max":
            return
        storage_section = "storage"
        if ini_config.has_option(storage_section, "data_path"):
            ini_config.remove_option(storage_section, "data_path")
        ini_config[storage_section]["type"] = "tikv"
        ini_config[storage_section]["pd_addrs"] = node_config.pd_addrs

    def __update_storage_security_info(self, ini_config, node_config, node_type):
        """
        update the storage_security for config.ini
        """
        section = "storage_security"
        # not support storage_security for max-node
        if node_type == "max":
            if ini_config.has_section(section):
                ini_config.remove_section(section)
            return
        ini_config[section]["enable"] = utilities.convert_bool_to_str(
            node_config.enable_storage_security)
        ini_config[section]["key_center_url"] = node_config.key_center_url
        ini_config[section]["cipher_data_key"] = node_config.cipher_data_key

    def __generate_pem_file(self, outputdir, node_config):
        """
        generate private key to the given path
        """
        pem_path = os.path.join(outputdir, self.node_pem_file)
        node_id_path = os.path.join(outputdir, self.node_id_file)
        utilities.generate_private_key(
            node_config.sm_crypto, outputdir)
        node_id = ""
        with open(node_id_path, 'r', encoding='utf-8') as f:
            node_id = f.read()
        return (node_id, pem_path, node_id_path)

    def get_config_file_path_list(self, node_service_config, node_config):
        """
        get config file path for given config files
        """
        path = os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_service_config.service_name)
        config_file_path_list = []
        for config in node_service_config.config_file_list:
            config_file_path_list.append(os.path.join(path, config))
        return config_file_path_list

    def __get_and_generate_node_base_path(self, node_config):
        path = os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_config.node_service.service_name)
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def generate_node_pem(self, node_config):
        """
        generate private key for the node
        """
        path = self.__get_and_generate_node_base_path(node_config)
        return self.__generate_pem_file(path, node_config)

    def generate_all_ini_config(self, group_config):
        """
        generate all ini config file
        """
        for node_config in group_config.node_list:
            if self.__generate_and_store_ini_config(node_config, group_config) is False:
                return False
        return True

    def _generate_all_node_pem(self, group_config):
        """
        generate all node.pem and return the nodeID
        """
        nodeid_list = []
        for node_config in group_config.node_list:
            (node_id, node_pem_path, node_id_path) = self.generate_node_pem(
                node_config)
            utilities.log_info(
                "* generate pem file for %s\n\t- pem_path: %s\n\t- node_id_path: %s\n\t- node_id: %s\n\t- sm_crypto: %d" % (node_config.node_service.service_name, node_pem_path, node_id_path, node_id, group_config.sm_crypto))
            nodeid_list.append(node_id)
        return nodeid_list

    def __genesis_config_generated(self, group_config):
        if os.path.exists(group_config.genesis_config_path):
            return True
        return False

    def generate_genesis_config(self, group_config, enforce_genesis_exists):
        if self.__genesis_config_generated(group_config):
            config_content = configparser.ConfigParser(
                comment_prefixes='/', allow_no_value=True)
            config_content.read(group_config.genesis_config_path)
            self._generate_all_node_pem(group_config)
            return config_content
        if enforce_genesis_exists is True:
            utilities.log_error("Please set the genesis config path firstly!")
            sys.exit(-1)
        nodeid_list = self._generate_all_node_pem(group_config)
        return self.generate_genesis_config_nodeid(nodeid_list, group_config)

    def generate_all_config(self, enforce_genesis_exists):
        """
        generate all config for max-node
        """
        for group_config in self.config.group_list.values():
            utilities.print_badage(
                "generate genesis config for group %s" % group_config.group_id)
            if self.generate_all_genesis_config(group_config, enforce_genesis_exists) is False:
                return False
            utilities.print_badage(
                "generate genesis config for %s success" % group_config.group_id)
            utilities.print_badage(
                "generate ini config for group %s" % group_config.group_id)
            if self.generate_all_ini_config(group_config) is False:
                return False
            utilities.print_badage(
                "generate ini config for group %s success" % group_config.group_id)
        return True

    def generate_all_genesis_config(self, group_config, enforce_genesis_exists):
        """
        generate the genesis config for all max_nodes
        """
        genesis_config_content = self.generate_genesis_config(
            group_config, enforce_genesis_exists)
        if os.path.exists(group_config.genesis_config_path) is False:
            desc = group_config.chain_id + "." + group_config.group_id
            self.store_config(genesis_config_content, "genesis",
                              group_config.genesis_config_path, desc)
        for node_config in group_config.node_list:
            node_path = self.__get_and_generate_node_base_path(node_config)
            genesis_config_path = os.path.join(
                node_path, self.genesis_tmp_config_file)
            if self.store_config(genesis_config_content, "genesis", genesis_config_path, node_config.node_service.service_name) is False:
                return False
        return True

    def store_config(self, config_content, config_type, config_path, desc):
        """
        store the generated genesis config content for given node
        """
        if os.path.exists(config_path):
            utilities.log_error("* store %s config for %s failed for the config %s already exists." %
                                (config_type, desc, config_path))
            return False
        utilities.log_info("* store %s config for %s\n\t path: %s" %
                           (config_type, desc, config_path))
        with open(config_path, 'w') as configFile:
            config_content.write(configFile)
            utilities.log_info("* store %s config for %s success" %
                               (config_type, desc))
        return True

    def __generate_and_store_ini_config(self, node_config, group_config):
        """
        generate and store ini config for given node
        """
        ini_config_content = self.__generate_node_config(
            group_config, node_config, node_config.node_service.service_name, self.node_type)
        node_path = self.__get_and_generate_node_base_path(node_config)
        ini_config_path = os.path.join(node_path, self.ini_tmp_config_file)
        return self.store_config(ini_config_content, "ini", ini_config_path, node_config.node_service.service_name)
