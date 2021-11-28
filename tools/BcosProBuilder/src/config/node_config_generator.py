#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
from common import utilities
from common.utilities import ConfigInfo
import shutil
import os
from service.tars_service import TarsService


class NodeConfigGenerator:
    def __init__(self, config):
        self.config = config
        self.genesis_tpl_config = ConfigInfo.genesis_config_tpl_path
        self.node_tpl_config = ConfigInfo.node_config_tpl_path
        self.genesis_config_file = "config.genesis"
        self.genesis_tmp_config_file = 'config.genesis.tmp'
        self.ini_config_file = "config.ini"
        self.ini_config_tmp_file = "config.ini.tmp"
        self.node_pem_file = "node.pem"
        self.node_id_file = "node.nodeid"
        self.root_dir = "generated/"

    def get_all_service_info(self, node_config, service_name):
        config_file_list = [
            self.ini_config_file, self.genesis_config_file, self.node_id_file, self.node_pem_file]
        node_id_path = os.path.join(self.get_node_pem_path(
            node_config, service_name), self.node_id_file)
        node_pem_path = os.path.join(self.get_node_pem_path(
            node_config, service_name), self.node_pem_file)
        config_path_list = [self.get_node_ini_config_path(
            node_config, service_name), self.get_genesis_config_path(node_config, service_name), node_id_path, node_pem_path]
        return (config_file_list, config_path_list)

    def get_genesis_config_path(self, node_config, service_name):
        return self.get_node_config_path(node_config, service_name, self.genesis_tmp_config_file)

    def get_node_ini_config_path(self, node_config, service_name):
        return self.get_node_config_path(node_config, service_name, self.ini_config_tmp_file)

    def get_node_config_path(self, node_config, service_name, file_name):
        return os.path.join(self.root_dir, self.config.chain_id, self.config.group_config.group_id, node_config.deploy_ip, service_name, file_name)

    def get_node_config_dir(self, node_config, service_name):
        return os.path.join(self.root_dir, self.config.chain_id, self.config.group_config.group_id, node_config.deploy_ip, service_name)

    def generate_genesis_config(self, nodeid_list):
        """
        generate the gensis config
        """
        config_content = configparser.ConfigParser()
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
        config_content[tx_section]["gas_limit"] = self.config.group_config.genesis_config.gas_limit
        return config_content

    def generate_node_config(self, node_config, node_name):
        """
        generate node config: config.ini.tmp
        """
        ini_config = configparser.ConfigParser()
        ini_config.read(self.node_tpl_config)
        chain_section = "chain"
        ini_config[chain_section]["sm_crypto"] = utilities.convert_bool_to_str(
            self.config.group_config.sm_crypto)
        ini_config[chain_section]["group_id"] = self.config.group_config.group_id
        ini_config[chain_section]["chain_id"] = self.config.group_config.chain_id
        service_section = "service"
        ini_config[service_section]["node_name"] = node_name
        ini_config[service_section]["rpc"] = self.config.chain_id + \
            "." + node_config.rpc_service_name
        ini_config[service_section]["gateway"] = self.config.chain_id + \
            "." + node_config.gateway_service_name
        # generate the node name
        if node_config.microservice_node is True:
            node_service_config_item = node_config.node_service_config_info[node_name]
            for config_key in node_service_config_item.keys():
                ini_config[service_section][config_key] = self.config.chain_id + \
                    "." + node_service_config_item[config_key]

        executor_section = "executor"
        if self.config.group_config.vm_type == "evm":
            ini_config[executor_section]["is_wasm"] = utilities.convert_bool_to_str(
                False)
        if self.config.group_config.vm_type == "wasm":
            ini_config[executor_section]["is_wasm"] = utilities.convert_bool_to_str(
                True)
        return ini_config

    def generate_node_ini_config(self, node_config):
        for node_name in node_config.node_service_config_info.keys():
            ini_config = self.generate_node_config(node_config, node_name)
            service_list = node_config.nodes_service_name_list[node_name]
            for service in service_list:
                file_path = self.get_node_config_path(
                    node_config, service, self.ini_config_tmp_file)
                if os.path.exists(file_path):
                    utilities.log_error(
                        "* generate ini config for service %s failed, for the config file %s already exists!" % (service, file_path))
                    return False
                utilities.log_info(
                    "* generate ini config for service %s\n\tconfig path: %s" % (service, file_path))
                utilities.mkfiledir(file_path)
                with open(file_path, 'w') as configfile:
                    ini_config.write(configfile)
        return True

    def write_config_to_path(self, config_content, config_file_path):
        if os.path.exists(config_file_path):
            utilities.log_error(
                "write config to %s failed for already exists" % config_file_path)
            return False
        utilities.mkfiledir(config_file_path)
        with open(config_file_path, 'w') as config_file_path:
            config_file_path.write(config_content)
        return True

    def reload_node_config_for_expanded_node(self, node_config, ini_config_content, node_name):
        """
        reload node_config after fetch iniConfig
        """
        ini_config = configparser.ConfigParser()
        ini_config.read_string(ini_config_content)
        chain_section = "chain"
        node_config.group_id = ini_config[chain_section]["group_id"]
        self.config.group_id = node_config.group_id

        node_config.chain_id = ini_config[chain_section]["chain_id"]
        self.config.chain_id = node_config.chain_id

        self.config.group_config.group_id = node_config.group_id
        self.config.group_config.chain_id = node_config.chain_id

        sm_crypto = ini_config[chain_section]["sm_crypto"]
        self.config.group_config.sm_crypto = False
        node_config.generate_service_name_list()
        node_config.generate_service_config_info()
        if sm_crypto == "true":
            self.config.group_config.sm_crypto = True
        # reset the node_name for the ini config
        ini_config["service"]["node_name"] = node_name
        return ini_config

    def generate_expand_node_config(self, node_config):
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, "")
        # fetch the ini config
        (ret, ini_config_content) = tars_service.fetch_server_config_file(
            self.ini_config_file, node_config.expanded_service)
        if ret is False:
            utilities.log_error(
                "* expand node failed for fetch ini config from %s failed" % node_config.expanded_service)
            return False
        # fetch the genesis config
        (ret, genesis_config_content) = tars_service.fetch_server_config_file(
            self.genesis_config_file, node_config.expanded_service)
        if ret is False:
            utilities.log_error(
                "* expand node failed for fetch genesis config from %s failed" % node_config.expanded_service)
            return False
        # load group_id and crypto_type from the config
        # reload the config
        utilities.log_info("* reload node config")
        for node_name in node_config.node_service_config_info.keys():
            updated_ini_config = self.reload_node_config_for_expanded_node(
                node_config, ini_config_content, node_name)
            # Note: obtain updated service_list after reload node_config
            service_list = node_config.nodes_service_name_list[node_name]
            for service in service_list:
                ini_config_path = self.get_node_config_path(
                    node_config, service, self.ini_config_tmp_file)
                utilities.log_info(
                    "* generate ini config, service: %s, path: %s" % (service, ini_config_path))
                # if self.write_config_to_path(ini_config_content, ini_config_path) is False:
                #    return False
                if os.path.exists(ini_config_path):
                    utilities.log_error(
                        "* generate ini config for %s failed for the config %s already exists." % (service, ini_config_path))
                    return False
                utilities.mkfiledir(ini_config_path)
                with open(ini_config_path, 'w') as configfile:
                    updated_ini_config.write(configfile)
                utilities.log_info(
                    "* generate ini config for service: %s success" % service)
                # generate genesis config
                genesis_config_path = self.get_node_config_path(
                    node_config, service, self.genesis_tmp_config_file)
                utilities.log_info(
                    "* generate genesis config, service: %s, path: %s" % (service, genesis_config_path))
                if self.write_config_to_path(genesis_config_content, genesis_config_path) is False:
                    return False
                utilities.log_info(
                    "* generate genesis config for service: %s success" % service)
        return True

    def generate_node_genesis_config(self, node_config, nodeid_list):
        genesis_config = self.generate_genesis_config(nodeid_list)
        return self.write_config(
            genesis_config, self.genesis_tmp_config_file, node_config)

    def write_config(self, updated_config, file_name, node_config):
        for key in node_config.nodes_service_name_list.keys():
            service_list = node_config.nodes_service_name_list[key].keys()
            for service in service_list:
                file_path = self.get_node_config_path(
                    node_config, service, file_name)
                if os.path.exists(file_path):
                    utilities.log_error(
                        "* generate genesis config for %s failed for the config %s already exists." % (service, file_path))
                    return False
                utilities.log_info(
                    "* generate genesis config for %s\n\t path: %s" % (service, file_path))
                utilities.mkfiledir(file_path)
                with open(file_path, 'w') as configfile:
                    updated_config.write(configfile)
        return True

    def generate_all_nodes_pem(self):
        nodeid_list = []
        for node_config in self.config.group_config.node_list:
            single_node_list = self.generate_node_pem(node_config)
            nodeid_list = nodeid_list + single_node_list
        return nodeid_list

    def generate_node_all_config(self, node_config, nodeid_list):
        if self.generate_node_genesis_config(node_config, nodeid_list) is False:
            return False
        return self.generate_node_ini_config(node_config)

    def get_node_pem_path(self, node_config, service_name):
        return os.path.join(self.root_dir, self.config.group_config.chain_id, self.config.group_config.group_id, node_config.deploy_ip, service_name)

    def generate_node_pem(self, node_config):
        """
        generate private key for the node
        """
        outputdir = self.root_dir
        pem_path = os.path.join(outputdir, self.node_pem_file)
        node_id_path = os.path.join(outputdir, self.node_id_file)
        nodeid_list = []
        for key in node_config.nodes_service_name_list.keys():
            utilities.generate_private_key(
                self.config.group_config.sm_crypto, outputdir)
            with open(node_id_path, 'r', encoding='utf-8') as f:
                nodeid_list.append(f.read())
            single_node_service = (
                node_config.nodes_service_name_list[key]).keys()
            for service in single_node_service:
                dst_path = self.get_node_pem_path(node_config, service)
                utilities.log_info(
                    "* generate pem file for %s\n\t- pem_path: %s\n\t- node_id_path: %s\n\t- sm_crypto: %d" % (service, dst_path, node_id_path, self.config.group_config.sm_crypto))
                # copy the generated file to all services path
                utilities.mkdir(dst_path)
                shutil.copy(pem_path, dst_path)
                shutil.copy(node_id_path, dst_path)
        return nodeid_list
