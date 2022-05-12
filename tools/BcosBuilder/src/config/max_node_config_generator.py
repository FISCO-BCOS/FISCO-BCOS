#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities
from config.common_node_config_generator import CommonConfigGenerator
import os


class MaxNodeConfigGenerator:
    def __init__(self, chain_config):
        self.chain_config = chain_config
        self.config_generator = CommonConfigGenerator(chain_config)
        self.root_dir = "./generated"
        self.genesis_tmp_config_file = 'config.genesis'
        self.ini_tmp_config_file = "config.ini"

    def generate_all_config(self):
        """
        generate all config for max-node
        """
        utilities.print_badage("generate genesis config")
        if self.__generate_all_genesis_config() is False:
            return False
        utilities.print_badage("generate genesis config success")
        utilities.print_badage("generate ini config for BcosMaxNodeService")
        if self.__generate_all_ini_config() is False:
            return False
        utilities.print_badage(
            "generate ini config for BcosMaxNodeService success")
        utilities.print_badage("generate ini config for BcosExecutorService")
        if self.__generate_all_executor_config() is False:
            return False
        utilities.print_badage("generate ini config for BcosExecutorService")
        return True

    def get_config_file_path_list(self, max_node_service_config, max_node_config):
        """
        get config file path for given config files
        """
        path = os.path.join(self.root_dir, max_node_config.chain_id,
                            max_node_config.group_id, max_node_service_config.service_name)
        config_file_path_list = []
        for config in max_node_service_config.config_file_list:
            config_file_path_list.append(os.path.join(path, config))
        return config_file_path_list

    def __generate_all_executor_config(self):
        """
        generate the config for all executor service
        """
        for max_node_config in self.chain_config.group_config.max_node_list:
            if self.__generate_executor_config(max_node_config) is False:
                return False
        return True

    def __generate_executor_config(self, max_node_config):
        executor_config_content = self.config_generator.generate_executor_config(
            max_node_config, max_node_config.executor_service.service_name)
        executor_dir = self.__get_and_generate_executor_base_path(
            max_node_config)
        executor_config_path = os.path.join(
            executor_dir, self.ini_tmp_config_file)
        return self.__store_config(executor_config_content, "executor ini", executor_config_path, max_node_config)

    def __generate_all_genesis_config(self):
        """
        generate the genesis config for all max_nodes
        """
        genesis_config_content = self.__generate_genesis_config()
        for max_node_config in self.chain_config.group_config.max_node_list:
            node_path = self.__get_and_generate_node_base_path(max_node_config)
            genesis_config_path = os.path.join(
                node_path, self.genesis_tmp_config_file)
            if self.__store_config(genesis_config_content, "genesis", genesis_config_path, max_node_config) is False:
                return False
        return True

    def __generate_all_ini_config(self):
        """
        generate all ini config file
        """
        for max_node_config in self.chain_config.group_config.max_node_list:
            if self.__generate_and_store_ini_config(max_node_config) is False:
                return False
        return True

    def __generate_and_store_ini_config(self, max_node_config):
        """
        generate and store ini config for given node
        """
        ini_config_content = self.config_generator.generate_node_config(
            max_node_config, max_node_config.max_node_service.service_name, "max")
        node_path = self.__get_and_generate_node_base_path(max_node_config)
        ini_config_path = os.path.join(node_path, self.ini_tmp_config_file)
        return self.__store_config(ini_config_content, "ini", ini_config_path, max_node_config)

    def __store_config(self, config_content, config_type, config_path, max_node_config):
        """
        store the generated genesis config content for given node
        """
        if os.path.exists(config_path):
            utilities.log_error("* store %s config for %s failed for the config %s already exists." %
                                (config_type, max_node_config.max_node_service.service_name, config_path))
            return False
        utilities.log_info("* store %s config for %s\n\t path: %s" %
                           (config_type, max_node_config.max_node_service.service_name, config_path))
        with open(config_path, 'w') as configFile:
            config_content.write(configFile)
            utilities.log_info("* store %s config for %s success" %
                               (config_type, max_node_config.max_node_service.service_name))
        return True

    def __generate_genesis_config(self):
        nodeid_list = self._generate_all_node_pem()
        return self.config_generator.generate_genesis_config(nodeid_list)

    def _generate_all_node_pem(self):
        """
        generate all node.pem and return the nodeID
        """
        nodeid_list = []
        for max_node_config in self.chain_config.group_config.max_node_list:
            (node_id, node_pem_path, node_id_path) = self.__generate_node_pem(
                max_node_config)
            utilities.log_info(
                "* generate pem file for %s\n\t- pem_path: %s\n\t- node_id_path: %s\n\t- node_id: %s\n\t- sm_crypto: %d" % (max_node_config.max_node_service.service_name, node_pem_path, node_id_path, node_id, self.chain_config.group_config.sm_crypto))
            nodeid_list.append(node_id)
        return nodeid_list

    def __get_and_generate_node_base_path(self, max_node_config):
        path = os.path.join(self.root_dir, max_node_config.chain_id,
                            max_node_config.group_id, max_node_config.max_node_service.service_name)
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def __get_and_generate_executor_base_path(self, max_node_config):
        path = os.path.join(self.root_dir, max_node_config.chain_id,
                            max_node_config.group_id, max_node_config.executor_service.service_name)
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def __generate_node_pem(self, max_node_config):
        """
        generate private key for the node
        """
        path = self.__get_and_generate_node_base_path(max_node_config)
        return self.config_generator.generate_pem_file(path)
