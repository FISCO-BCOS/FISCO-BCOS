#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
import platform
import shutil
from common import utilities
from common.utilities import ConfigInfo
from service.key_center_service import KeyCenterService
import uuid
import os
import sys

from common.utilities import execute_command_and_getoutput
from common.utilities import mkdir
from config.tars_install_package_generator import get_tars_proxy_config_section_index
from config.tars_install_package_generator import generate_tars_package
from config.tars_install_package_generator import generate_tars_proxy_config
from config.tars_install_package_generator import initialize_tars_config_env_variables



class NodeConfigGenerator:
    """
    the common node config generator
    """

    def __init__(self, config, node_type, output_dir, is_build_opr=False):
        self.config = config
        self.genesis_tpl_config = ConfigInfo.genesis_config_tpl_path
        self.node_tpl_config = ConfigInfo.node_config_tpl_path
        self.executor_tpl_config = ConfigInfo.executor_config_tpl_path
        self.node_pem_file = "node.pem"
        self.node_id_file = "node.nodeid"
        self.output_dir = output_dir
        self.root_dir = output_dir
        self.genesis_tmp_config_file = 'config.genesis'
        self.ini_tmp_config_file = "config.ini"
        self.node_type = node_type
        self.is_build_opr = is_build_opr

    def generate_genesis_config_nodeid(self, nodeid_list, group_config):
        """
        generate the genesis config
        """
        utilities.log_info("* generate genesis config nodeid")
        config_content = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        config_content.read(self.genesis_tpl_config)
        chain_section = "chain"
        config_content[chain_section]["sm_crypto"] = utilities.convert_bool_to_str(
            group_config.genesis_config.sm_crypto)
        config_content[chain_section]["group_id"] = str(
            group_config.genesis_config.group_id)
        config_content[chain_section]["chain_id"] = str(
            group_config.genesis_config.chain_id)
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

        utilities.log_info("* chain_id: %s" %
                           config_content[chain_section]["group_id"])
        utilities.log_info("* group_id: %s" %
                           config_content[chain_section]["chain_id"])
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
        executor_ini_config[service_section]['without_tars_framework'] = "true" if self.is_build_opr else "false"
        executor_ini_config[service_section]['tars_proxy_conf'] = 'conf/tars_proxy.ini'
        executor_ini_config[service_section]["node_name"] = node_name
        executor_ini_config[service_section]["scheduler"] = self.config.chain_id + \
                                                            "." + node_config.scheduler_service_name
        executor_ini_config[service_section]["txpool"] = self.config.chain_id + \
                                                         "." + node_config.txpool_service_name
        # executor config
        self.__update_storage_info(
            executor_ini_config, node_config, self.node_type)
        return executor_ini_config

    def generate_node_config(self, group_config, node_config, node_name, node_type):
        """
        generate node config: config.ini.tmp
        """
        ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        ini_config.read(self.node_tpl_config)
        # self.__update_chain_info(ini_config, node_config)
        self.__update_service_info(ini_config, node_config, node_name)
        self.__update_failover_info(ini_config, node_config, node_type)
        # set storage config
        self.__update_storage_info(ini_config, node_config, node_type)
        # set storage_security config
        # access key_center to encrypt the certificates and the private keys
        self.__update_storage_security_info(ini_config, node_config, node_type)
        self.__update_hsm_info(ini_config, node_config)
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
        ini_config["service"]['without_tars_framework'] = "true" if self.is_build_opr else "false"
        ini_config["service"]['tars_proxy_conf'] = 'conf/tars_proxy.ini'
        ini_config[service_section]["rpc"] = self.config.chain_id + \
                                             "." + node_config.agency_config.rpc_service_name
        ini_config[service_section]["gateway"] = self.config.chain_id + \
                                                 "." + node_config.agency_config.gateway_service_name
        if hasattr(node_config, 'executor_service'):
            ini_config[service_section]["executor"] = self.config.chain_id + \
                                                      "." + node_config.executor_service.service_name

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
        ini_config[storage_section]["key_page_size"] = str(
            node_config.key_page_size)

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

    def __update_hsm_info(self, ini_config, node_config):
        """
        update the security hsm for config.ini
        """
        section = "security"
        ini_config[section]["enable_hsm"] = utilities.convert_bool_to_str(
            node_config.enable_hsm)
        ini_config[section]["hsm_lib_path"] = node_config.hsm_lib_path
        ini_config[section]["key_index"] = str(node_config.hsm_key_index)
        ini_config[section]["password"] = node_config.hsm_password
        ini_config[section]["hsm_public_key_file_path"] = node_config.hsm_public_key_file_path

    def __generate_pem_file(self, outputdir, node_config):
        """
        generate private key to the given path
        """
        pem_path = os.path.join(outputdir, self.node_pem_file)
        node_id_path = os.path.join(outputdir, self.node_id_file)

        # if the file is not exist, generate it
        if os.path.exists(pem_path) is False or os.path.exists(node_id_path) is False:
            utilities.generate_private_key(
                node_config.sm_crypto, outputdir)
        # encrypt the node.pem with key_center
        if node_config.enable_storage_security is True:
            key_center = KeyCenterService(
                node_config.key_center_url, node_config.cipher_data_key)
            ret = key_center.encrypt_file(pem_path)
            if ret is False:
                return (False, "", pem_path, node_id_path)
        node_id = ""
        with open(node_id_path, 'r', encoding='utf-8') as f:
            node_id = f.read()
        return (True, node_id, pem_path, node_id_path)

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

    def get_ini_config_file_path(self, node_service_config, node_config, deploy_ip):
        """
        get config file path for given config files
        """
        return os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_service_config.service_name, deploy_ip,
                            node_service_config.ini_config_file)

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

    def get_nodeid_from_pem_file(self, hsm_public_key_file_path):
        """
        get nodeid from node.pem that export from HSM
        """
        if os.path.exists(hsm_public_key_file_path) is False:
            utilities.log_error("hsm_public_key_file_path: %s don't exist, please check!\n\t" % hsm_public_key_file_path)
            sys.exit(-1)
        (ret, output) = utilities.get_hsm_nodeid(hsm_public_key_file_path)
        if ret is False:
            return (False, "")
        node_id = None
        for line in output.split('\n'):
            if line.startswith('nodeid='):
                node_id = line[len('nodeid='):]
        return (True, node_id)

    def _generate_all_node_pem(self, group_config):
        """
        generate all node.pem and return the nodeID
        """
        nodeid_list = []
        for node_config in group_config.node_list:
            if node_config.enable_hsm is True:
                (ret, node_id) = self.get_nodeid_from_pem_file(node_config.hsm_public_key_file_path)
                if ret is False:
                    return (False, nodeid_list)
                path = self.__get_and_generate_node_base_path(node_config)
                shutil.copy(node_config.hsm_public_key_file_path, os.path.join(path, self.node_pem_file))
            else:
                (ret, node_id, node_pem_path, node_id_path) = self.generate_node_pem(
                    node_config)
                if ret is False:
                    return (False, nodeid_list)
                utilities.log_info(
                    "* generate pem file for %s\n\t- pem_path: %s\n\t- node_id_path: %s\n\t- node_id: %s\n\t- sm_crypto: %d" % (
                        node_config.node_service.service_name, node_pem_path, node_id_path, node_id,
                        group_config.sm_crypto))
            nodeid_list.append(node_id)
        return (True, nodeid_list)

    def __genesis_config_generated(self, group_config):
        if os.path.exists(group_config.genesis_config_path):
            utilities.log_info(
                "* the genesis config file has been set, path: %s" % group_config.genesis_config_path)
            return True
        return False

    def generate_genesis_config(self, group_config, must_genesis_exists):
        if self.__genesis_config_generated(group_config):
            config_content = configparser.ConfigParser(
                comment_prefixes='/', allow_no_value=True)
            config_content.read(group_config.genesis_config_path)
            (ret, nodeid_list) = self._generate_all_node_pem(group_config)
            return (ret, config_content)
        if must_genesis_exists is True:
            utilities.log_error("Please set the genesis config path firstly!")
            sys.exit(-1)
        (ret, nodeid_list) = self._generate_all_node_pem(group_config)
        if ret is False:
            return (False, None)
        config_content = self.generate_genesis_config_nodeid(
            nodeid_list, group_config)
        return (True, config_content)

    def generate_all_config(self, enforce_genesis_exists):
        """
        generate all config for max-node
        """
        for group_config in self.config.group_list.values():
            utilities.print_badge(
                "generate genesis config for group %s" % group_config.group_id)
            if self.generate_all_genesis_config(group_config, enforce_genesis_exists) is False:
                return False
            utilities.print_badge(
                "generate genesis config for %s success" % group_config.group_id)
            utilities.print_badge(
                "generate ini config for group %s" % group_config.group_id)
            if self.generate_all_ini_config(group_config) is False:
                return False
            utilities.print_badge(
                "generate ini config for group %s success" % group_config.group_id)
        return True

    def generate_all_genesis_config(self, group_config, enforce_genesis_exists):
        """
        generate the genesis config for all max_nodes
        """
        (ret, genesis_config_content) = self.generate_genesis_config(
            group_config, enforce_genesis_exists)
        if ret is False:
            return False
        if os.path.exists(group_config.genesis_config_path) is False:
            desc = group_config.chain_id + "." + group_config.group_id
            self.store_config(genesis_config_content, "genesis",
                              group_config.genesis_config_path, desc, False)
        for node_config in group_config.node_list:
            node_path = self.__get_and_generate_node_base_path(node_config)
            genesis_config_path = os.path.join(
                node_path, self.genesis_tmp_config_file)
            if self.store_config(genesis_config_content, "genesis", genesis_config_path,
                                 node_config.node_service.service_name, False) is False:
                return False
        return True

    def store_config(self, config_content, config_type, config_path, desc, ignore_if_exists):
        """
        store the generated genesis config content for given node
        """
        if os.path.exists(config_path) and ignore_if_exists is False:
            utilities.log_error("* store %s config for %s failed for the config %s already exists." %
                                (config_type, desc, config_path))
            return False
        utilities.log_info("* store %s config for %s\n\t path: %s" %
                           (config_type, desc, config_path))

        if os.path.exists(os.path.dirname(config_path)) is False:
            utilities.mkdir(os.path.dirname(config_path))

        with open(config_path, 'w') as configFile:
            config_content.write(configFile)
            utilities.log_info("* store %s config for %s success" %
                               (config_type, desc))
        return True

    def get_and_generate_ini_config_base_path(self, node_config, deploy_ip):
        path = os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_config.node_service.service_name, deploy_ip)
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def get_and_generate_install_package_path(self, group_config, node_config, deploy_ip, name):
        path = os.path.join(self.root_dir, deploy_ip,
                            group_config.group_id + "_" + name + "_" + str(node_config.tars_listen_port))
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def generate_all_tars_install_package(self):
        """
        generate all install package for node
        """
        if not self.generate_all_config(False):
            return False
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                deploy_ip = node_config.deploy_ip
                node_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, 'node')
                utilities.log_info(" * generate node install package for deploy_ip: %s:%s:%s" % (
                    deploy_ip, node_dir, node_config.node_service_name))
                self.generate_node_tars_install_package(group_config, node_config, deploy_ip, 'node')
                base_dir = self.get_and_generate_ini_config_base_path(node_config, '')
                config_path = os.path.join(base_dir, self.ini_tmp_config_file)
                genesis_config_path = os.path.join(base_dir, self.genesis_tmp_config_file)
                nodeid_path = os.path.join(base_dir, 'node.nodeid')
                node_pem_path = os.path.join(base_dir, 'node.pem')
                utilities.mkdir(os.path.join(node_dir, 'conf'))

                shutil.copy(config_path, os.path.join(node_dir, 'conf'))
                shutil.copy(genesis_config_path, os.path.join(node_dir, 'conf'))
                shutil.copy(nodeid_path, os.path.join(node_dir, 'conf'))
                shutil.copy(node_pem_path, os.path.join(node_dir, 'conf'))

    def copy_tars_proxy_conf(self):
        self.__copy_service_tars_proxy_conf()

    def __copy_service_tars_proxy_conf(self):
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                agency_name = node_config.agency_config.name
                tars_proxy_conf = os.path.join(self.output_dir, node_config.agency_config.chain_id,
                                               agency_name + "_tars_proxy.ini")
                executor_dir = self.get_and_generate_install_package_path(group_config, node_config, node_config.deploy_ip,
                                                                            'node')
                shutil.copy(tars_proxy_conf, os.path.join(executor_dir, 'conf', 'tars_proxy.ini'))
                utilities.log_info("* copy node tars_proxy.ini: " + tars_proxy_conf + " ,dir: " + executor_dir)

    def __generate_and_store_ini_config(self, node_config, group_config):
        """
        generate and store ini config for given node
        """
        ini_config_content = self.generate_node_config(
            group_config, node_config, node_config.node_service.service_name, self.node_type)
        node_path = self.__get_and_generate_node_base_path(node_config)

        if os.path.exists(node_path) is False:
            utilities.mkdir(node_path)

        ini_config_path = os.path.join(node_path, self.ini_tmp_config_file)
        return self.store_config(ini_config_content, "ini", ini_config_path, node_config.node_service.service_name,
                                 False)

    def generate_node_tars_install_package(self, group_config, node_config, deploy_ip, name):

        base_dir = self.get_and_generate_install_package_path(group_config, node_config, deploy_ip, name)
        utilities.log_info("=> base_dir: %s" % base_dir)

        utilities.log_info("* generate tars node install package service: %s, chain id: %s, tars pkg dir: %s" % (
            node_config.node_service.service_name, self.config.chain_id, self.config.tars_config.tars_pkg_dir))

        # install package
        generate_tars_package(base_dir, node_config.node_service_base_name, node_config.node_service_name, node_config.agency_config.name, self.config.chain_id, "node",
                              self.config.tars_config.tars_pkg_dir)

        config_items = {"@TARS_LISTEN_IP@": deploy_ip,
                        "@TXPOOL_LISTEN_PORT@": str(node_config.tars_listen_port + 0),
                        "@SCHEDULER_LISTEN_PORT@": str(node_config.tars_listen_port + 1),
                        "@PBFT_LISTEN_PORT@": str(node_config.tars_listen_port + 2),
                        "@LEDGER_LISTEN_PORT@": str(node_config.tars_listen_port + 3),
                        "@FRONT_LISTEN_PORT@": str(node_config.tars_listen_port + 4),
                        }
        # init config env variables
        initialize_tars_config_env_variables(config_items, os.path.join(base_dir, 'conf', 'tars.conf'))

        service_ports_items = {"txpool": deploy_ip + ":" + str(node_config.tars_listen_port),
                               "scheduler": deploy_ip + ":" + str(node_config.tars_listen_port + 1),
                               "pbft": deploy_ip + ":" + str(node_config.tars_listen_port + 2),
                               "ledger": deploy_ip + ":" + str(node_config.tars_listen_port + 3),
                               "front": deploy_ip + ":" + str(node_config.tars_listen_port + 4),
                               }
        # generate tars proxy config
        generate_tars_proxy_config(self.output_dir, node_config.agency_config.name, node_config.agency_config.chain_id,
                                   service_ports_items)
