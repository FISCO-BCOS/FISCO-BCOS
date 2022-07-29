#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities
from config.node_config_generator import NodeConfigGenerator
import os


class MaxNodeConfigGenerator(NodeConfigGenerator):
    def __init__(self, chain_config, node_type, output_dir):
        NodeConfigGenerator.__init__(self, chain_config, node_type, output_dir)
        self.chain_config = chain_config
        self.root_dir = output_dir
        self.ini_tmp_config_file = "config.ini"
        self.genesis_tmp_config_file = 'config.genesis'

    def generate_all_config(self, enforce_genesis_exists):
        """
        generate all config for max-node
        """
        for group_config in self.chain_config.group_list.values():
            utilities.print_badge(
                "generate genesis config for group %s" % group_config.group_id)
            if self.generate_all_genesis_config(group_config, enforce_genesis_exists) is False:
                return False
            utilities.print_badge(
                "generate genesis config for %s success" % group_config.group_id)
            utilities.print_badge(
                "generate ini config for BcosMaxNodeService of group %s" % group_config.group_id)
            if self.generate_all_ini_config(group_config) is False:
                return False
            utilities.print_badge(
                "generate ini config for BcosMaxNodeService of group %s success" % group_config.group_id)
            utilities.print_badge(
                "generate ini config for BcosExecutorService of group %s" % group_config.group_id)
            if self.__generate_all_executor_config(group_config) is False:
                return False
            utilities.print_badge(
                "generate ini config for BcosExecutorService of group %s success" % group_config.group_id)
        return True

    def generate_all_executor_config(self):
        """
        generate all config for max-node
        """
        for group_config in self.chain_config.group_list.values():
            if self.__generate_all_executor_config(group_config) is False:
                return False
            utilities.print_badge(
                "generate ini config for BcosExecutorService of group %s success" % group_config.group_id)
        return True

    def __generate_all_executor_config(self, group_config):
        """
        generate the config for all executor service
        """
        for max_node_config in group_config.node_list:
            if self.__generate_executor_config(max_node_config, group_config) is False:
                return False
        return True

    def __generate_executor_config(self, max_node_config, group_config):
        (ret, executor_genesis_content) = self.generate_genesis_config(
            group_config, True)
        if ret is False:
            return False
        executor_config_content = self.generate_executor_config(
            group_config, max_node_config, max_node_config.executor_service.service_name)

        executor_dir = self.__get_and_generate_executor_base_path(
            max_node_config)
        executor_config_path = os.path.join(
            executor_dir, self.ini_tmp_config_file)
        executor_genesis_path = os.path.join(
            executor_dir, self.genesis_tmp_config_file)

        ini_store = self.store_config(executor_config_content, "executor ini",
                                      executor_config_path, max_node_config.executor_service.service_name, True)
        genesis_store = self.store_config(executor_genesis_content, "executor genesis",
                                          executor_genesis_path, max_node_config.executor_service.service_name, True)
        return ini_store and genesis_store

    def __get_and_generate_executor_base_path(self, node_config):
        path = os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_config.executor_service.service_name)
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path
