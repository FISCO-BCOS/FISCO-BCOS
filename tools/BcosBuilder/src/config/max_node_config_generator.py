#!/usr/bin/python
# -*- coding: UTF-8 -*-
import os
import shutil

from common import utilities

from config.node_config_generator import NodeConfigGenerator
from config.tars_install_package_generator import generate_tars_package
from config.tars_install_package_generator import generate_tars_proxy_config
from config.tars_install_package_generator import initialize_tars_config_env_variables

class MaxNodeConfigGenerator(NodeConfigGenerator):
    def __init__(self, chain_config, node_type, output_dir, is_build_opr = False):
        NodeConfigGenerator.__init__(self, chain_config, node_type, output_dir, is_build_opr)
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
            if self.__generate_max_node_ini_config(group_config) is False:
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

    def __generate_max_node_ini_config(self, group_config):
        """
        generate all ini config file
        """
        for node_config in group_config.node_list:
            if self.__generate_and_store_max_node_ini_config(node_config, group_config) is False:
                return False
        return True

    def __generate_and_store_max_node_ini_config(self, node_config, group_config):
        """
        generate and store ini config for given node
        """
        for ip in node_config.node_service.deploy_ip_list:
            ini_config_content = self.generate_node_config(
                group_config, node_config, node_config.node_service.service_name, self.node_type)
            node_path = self.get_and_generate_ini_config_base_path(node_config, ip)

            if os.path.exists(node_path) is False:
                utilities.mkdir(node_path)

            ini_config_path = os.path.join(node_path, self.ini_tmp_config_file)
            utilities.print_badge("generate ini config for ip %s ,path: %s" % (ip, ini_config_path))
            ret = self.store_config(ini_config_content, "ini", ini_config_path, node_config.node_service.service_name,
                                    False)
            if ret is False:
                utilities.log_error("generate ini config for ip %s failed", ip)
                return False
            utilities.print_badge("generate ini config for ip %s success" % ip)
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

    def generate_all_max_node_tars_install_package(self):
        """
        generate all install package for max node
        """
        if self.generate_all_config(False) is False:
            return False
        utilities.print_badge("generate max node install package begin")
        for group_config in self.chain_config.group_list.values():
            for node_config in group_config.node_list:
                for deploy_ip in node_config.node_service.deploy_ip_list:
                    node_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, 'max_node')
                    utilities.log_info("* generate max node install package for deploy_ip: %s, dir: %s" % (
                        deploy_ip, node_dir))
                    self.generate_node_tars_install_package(group_config, node_config, deploy_ip, 'max_node')
                    base_dir = self.get_and_generate_ini_config_base_path(node_config, deploy_ip)
                    config_path = os.path.join(base_dir, self.ini_tmp_config_file)
                    genesis_config_path = os.path.join(base_dir, '../', self.genesis_tmp_config_file)
                    nodeid_path = os.path.join(base_dir, '../', 'node.nodeid')
                    node_pem_path = os.path.join(base_dir, '../', 'node.pem')
                    utilities.mkdir(os.path.join(node_dir, 'conf'))

                    shutil.copy(config_path, os.path.join(node_dir, 'conf'))
                    shutil.copy(genesis_config_path, os.path.join(node_dir, 'conf'))
                    shutil.copy(nodeid_path, os.path.join(node_dir, 'conf'))
                    shutil.copy(node_pem_path, os.path.join(node_dir, 'conf'))
            utilities.print_badge("generate max node install package success")

    def generate_all_executor_tars_install_package(self):
        """
        generate all install package for executor
        """
        if self.generate_all_executor_config() is False:
            return False

        utilities.print_badge("generate executor install package begin")
        for group_config in self.chain_config.group_list.values():
            for node_config in group_config.node_list:
                for deploy_ip in node_config.executor_service.deploy_ip_list:
                    executor_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, 'executor')
                    utilities.log_info(" * generate executor install package for deploy_ip: %s, dir: %s" % (
                        deploy_ip, executor_dir))
                    self.__generate_executor_tars_install_package(group_config, node_config, deploy_ip)
                    base_dir = self.__get_and_generate_executor_base_path(node_config)
                    config_path = os.path.join(base_dir, self.ini_tmp_config_file)
                    genesis_config_path = os.path.join(base_dir, self.genesis_tmp_config_file)

                    shutil.copy(config_path, os.path.join(executor_dir, 'conf'))
                    shutil.copy(genesis_config_path, os.path.join(executor_dir, 'conf'))
            utilities.print_badge("generate executor install package success")

    def __generate_executor_tars_install_package(self, group_config, node_config, deploy_ip):

        pkg_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, 'executor')
        # install package
        generate_tars_package(pkg_dir, node_config.executor_service.base_service_name, node_config.executor_service.service_name, node_config.agency_config.name, self.config.chain_id, "executor",
                              self.config.tars_config.tars_pkg_dir)

        # executor tars listen port
        config_items = {
            "@TARS_LISTEN_IP@": deploy_ip,
            "@EXECUTOR_LISTEN_PORT@": str(node_config.tars_listen_port + 5)
        }
        # init config env variables
        initialize_tars_config_env_variables(config_items, os.path.join(pkg_dir, 'conf', 'tars.conf'))

        # executor tars listen port
        service_ports_items = {
            "executor": deploy_ip + ":" + str(node_config.tars_listen_port + 5)
        }
        # generate tars proxy config
        generate_tars_proxy_config(self.output_dir, node_config.agency_config.name, node_config.agency_config.chain_id,
                                   service_ports_items)

    def copy_executor_tars_proxy_conf(self):
        self.__copy_executor_tars_proxy_conf()

    def __copy_executor_tars_proxy_conf(self):
        for group_config in self.chain_config.group_list.values():
            for node_config in group_config.node_list:
                for deploy_ip in node_config.executor_service.deploy_ip_list:
                    executor_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, 'executor')
                    tars_proxy_conf = os.path.join(self.output_dir, node_config.agency_config.chain_id,
                                                   node_config.agency_config.name + "_tars_proxy.ini")
                    shutil.copy(tars_proxy_conf, os.path.join(executor_dir, 'conf', 'tars_proxy.ini'))
                    utilities.log_info("* copy executor tars_proxy.ini: " + tars_proxy_conf + " ,dir: " + executor_dir)

    def copy_max_node_tars_proxy_conf(self):
        self.__copy_max_node_tars_proxy_conf()

    def __copy_max_node_tars_proxy_conf(self):
        for group_config in self.chain_config.group_list.values():
            for node_config in group_config.node_list:
                for deploy_ip in node_config.node_service.deploy_ip_list:
                    node_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, "max_node")
                    tars_proxy_conf = os.path.join(self.output_dir, node_config.agency_config.chain_id,
                                                   node_config.agency_config.name + "_tars_proxy.ini")
                    shutil.copy(tars_proxy_conf, os.path.join(node_dir, 'conf', 'tars_proxy.ini'))
                    utilities.log_info("* copy max node tars_proxy.ini: " + tars_proxy_conf + " ,dir: " + node_dir)
