#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
import json
import platform
import shutil
from common import utilities
from common.utilities import ConfigInfo
from service.key_center_service import KeyCenterService
import uuid
import os
import sys

from common.utilities import execute_command_and_getoutput

class NodeConfigGenerator:
    """
    the common node config generator
    """

    def __init__(self, config, node_type, output_dir):
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

    def generate_genesis_config_nodeid(self, nodeid_list, group_config):
        """
        generate the genesis config
        """
        utilities.log_info("* generate genesis config nodeid")
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

    def __generate_node_config(self, group_config, node_config, node_name, node_type, is_build_opr):
        """
        generate node config: config.ini.tmp
        """
        ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        ini_config.read(self.node_tpl_config)
        self.__update_chain_info(ini_config, node_config)
        self.__update_service_info(ini_config, node_config, node_name, is_build_opr)
        self.__update_failover_info(ini_config, node_config, node_type)
        # set storage config
        self.__update_storage_info(ini_config, node_config, node_type)
        # set storage_security config
        # access key_center to encrypt the certificates and the private keys
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

    def __update_service_info(self, ini_config, node_config, node_name, is_build_opr):
        """
        update service info
        """
        service_section = "service"
        ini_config[service_section]["node_name"] = node_name
        ini_config["service"]['without_tars_framework'] = "true" if is_build_opr else "false"
        ini_config["service"]['tars_proxy_file'] = 'conf/tars_proxy.json'
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

    def __get_and_generate_node_base_path(self, node_config, is_build_opr):
        if not is_build_opr:
            path = os.path.join(self.root_dir, node_config.agency_config.chain_id,
                            node_config.group_id, node_config.node_service.service_name)
        else:
            path = os.path.join(self.output_dir, node_config.deploy_ip, node_config.group_id + "_node_" + str(node_config.tars_listen_port), "conf")
        if os.path.exists(path) is False:
            utilities.mkdir(path)
        return path

    def generate_node_pem(self, node_config, is_build_opr):
        """
        generate private key for the node
        """
        path = self.__get_and_generate_node_base_path(node_config, is_build_opr)
        return self.__generate_pem_file(path, node_config)

    def generate_all_ini_config(self, group_config, is_build_opr):
        """
        generate all ini config file
        """
        for node_config in group_config.node_list:
            if self.__generate_and_store_ini_config(node_config, group_config, is_build_opr) is False:
                return False
            if is_build_opr:
                self.__copy_tars_conf_and_bin_file(node_config, "BcosNodeService")
                self.__generate_tars_proxy_config(node_config)
        return True

    def _generate_all_node_pem(self, group_config, is_build_opr):
        """
        generate all node.pem and return the nodeID
        """
        nodeid_list = []
        for node_config in group_config.node_list:
            (ret, node_id, node_pem_path, node_id_path) = self.generate_node_pem(
                node_config, is_build_opr)
            if ret is False:
                return (False, nodeid_list)
            utilities.log_info(
                "* generate pem file for %s\n\t- pem_path: %s\n\t- node_id_path: %s\n\t- node_id: %s\n\t- sm_crypto: %d" % (node_config.node_service.service_name, node_pem_path, node_id_path, node_id, group_config.sm_crypto))
            nodeid_list.append(node_id)
        return (True, nodeid_list)

    def __genesis_config_generated(self, group_config):
        if os.path.exists(group_config.genesis_config_path):
            return True
        return False

    def generate_genesis_config(self, group_config, must_genesis_exists, is_build_opr = False):
        if self.__genesis_config_generated(group_config):
            config_content = configparser.ConfigParser(
                comment_prefixes='/', allow_no_value=True)
            config_content.read(group_config.genesis_config_path)
            (ret, nodeid_list) = self._generate_all_node_pem(group_config, is_build_opr)
            return (ret, config_content)
        if must_genesis_exists is True:
            utilities.log_error("Please set the genesis config path firstly!")
            sys.exit(-1)
        (ret, nodeid_list) = self._generate_all_node_pem(group_config, is_build_opr)
        if ret is False:
            return (False, None)
        config_content = self.generate_genesis_config_nodeid(
            nodeid_list, group_config)
        return (True, config_content)

    def generate_all_config(self, enforce_genesis_exists, is_build_opr):
        """
        generate all config for max-node
        """
        for group_config in self.config.group_list.values():
            utilities.print_badge(
                "generate genesis config for group %s" % group_config.group_id)
            if self.generate_all_genesis_config(group_config, enforce_genesis_exists, is_build_opr) is False:
                return False
            utilities.print_badge(
                "generate genesis config for %s success" % group_config.group_id)
            utilities.print_badge(
                "generate ini config for group %s" % group_config.group_id)
            if self.generate_all_ini_config(group_config, is_build_opr) is False:
                return False
            utilities.print_badge(
                "generate ini config for group %s success" % group_config.group_id)
        return True

    def generate_all_genesis_config(self, group_config, enforce_genesis_exists, is_build_opr):
        """
        generate the genesis config for all max_nodes
        """
        (ret, genesis_config_content) = self.generate_genesis_config(
            group_config, enforce_genesis_exists, is_build_opr)
        if ret is False:
            return False
        if os.path.exists(group_config.genesis_config_path) is False:
            desc = group_config.chain_id + "." + group_config.group_id
            self.store_config(genesis_config_content, "genesis",
                              group_config.genesis_config_path, desc, False)
        for node_config in group_config.node_list:
            node_path = self.__get_and_generate_node_base_path(node_config, is_build_opr)
            genesis_config_path = os.path.join(
                node_path, self.genesis_tmp_config_file)
            if self.store_config(genesis_config_content, "genesis", genesis_config_path, node_config.node_service.service_name, False) is False:
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
    
    def copy_tars_proxy_file(self):
        self.__copy_service_tars_proxy_file()
        
    def __copy_service_tars_proxy_file(self):
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                conf_dir = self.__get_and_generate_node_base_path(node_config, True)
                agency_name = node_config.agency_config.name
                tars_proxy_file = os.path.join(self.output_dir, node_config.agency_config.chain_id, agency_name + "_tars_proxy.json")
                copy_cmd = "cp " + tars_proxy_file + " " + conf_dir + "/tars_proxy.json"
                utilities.execute_command(copy_cmd)
                utilities.log_info("* copy tars_proxy.json: " + tars_proxy_file + " ,dir: " + conf_dir)
                

    def __generate_and_store_ini_config(self, node_config, group_config, is_build_opr):
        """
        generate and store ini config for given node
        """
        ini_config_content = self.__generate_node_config(
            group_config, node_config, node_config.node_service.service_name, self.node_type, is_build_opr)
        node_path = self.__get_and_generate_node_base_path(node_config, is_build_opr)

        if os.path.exists(node_path) is False:
            utilities.mkdir(node_path)
            
        ini_config_path = os.path.join(node_path, self.ini_tmp_config_file)
        return self.store_config(ini_config_content, "ini", ini_config_path, node_config.node_service.service_name, False)

    def __generate_tars_proxy_config(self, service_config):
        agency_name = service_config.agency_config.name
        chain_id = service_config.agency_config.chain_id
        tars_conf_dir = os.path.join(self.output_dir, chain_id)
        agency_tars_conf_path = os.path.join(tars_conf_dir, agency_name + "_tars_proxy.json")

        if os.path.exists(agency_tars_conf_path):
            with open(agency_tars_conf_path, 'r') as f:
                content = json.load(f)
        else:
            content = {}
        
        if "txpool" in content:
            content["txpool"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port))
        else:
            content["txpool"] = []
            content["txpool"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port))
        
        if "scheduler" in content:
            content["scheduler"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 1))
        else:
            content["scheduler"] = []
            content["scheduler"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 1))
        
        if "pbft" in content:
            content["pbft"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 2))
        else:
            content["pbft"] = []
            content["pbft"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 2))
        
        if "ledger" in content:
            content["ledger"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 3))
        else:
            content["ledger"] = []
            content["ledger"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 3))
        
        if "front" in content:
            content["front"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 4))
        else:
            content["front"] = []
            content["front"].append(service_config.deploy_ip + ":" + str(service_config.tars_listen_port + 4))
        
        with open(agency_tars_conf_path, 'w') as f:
            json.dump(content, f)

        return

    def __copy_tars_conf_and_bin_file(self, node_config, service_name):
        
        conf_dir = self.__get_and_generate_node_base_path(node_config, True)
        base_dir = os.path.dirname(conf_dir)

        # copy start.sh stop.sh tars.conf
        tars_start_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_start.sh")
        tars_stop_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_stop.sh")
        tars_conf_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_node.conf")

        tars_start_all_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_start_all.sh")
        tars_stop__all_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_stop_all.sh")

        start_all_file = os.path.join(base_dir, "../", "start_all.sh")
        stop_all_file = os.path.join(base_dir, "../", "stop_all.sh")

        start_file = os.path.join(base_dir, "start.sh")
        stop_file = os.path.join(base_dir, "stop.sh")
        conf_file = os.path.join(conf_dir, "tars.conf")
    
        shutil.copy(tars_start_file, start_file)
        shutil.copy(tars_stop_file, stop_file)
        shutil.copy(tars_conf_file, conf_file)

        if not os.path.exists(start_all_file):
            shutil.copy(tars_start_all_file, start_all_file)
            
        if not os.path.exists(stop_all_file):
            shutil.copy(tars_stop__all_file, stop_all_file)

        # copy service binary exec
        shutil.copy(os.path.join(self.config.tars_config.tars_pkg_dir, service_name), base_dir)

        sys_name = platform.system()
        if sys_name.lower() == "darwin":
            sed = "sed -i .bak "
        else:
            sed = "sed -i "

        sed_cmd = sed + "s/@SERVICE_NAME@/" + service_name + "/g " + start_file
        execute_command_and_getoutput(sed_cmd)

        sed_cmd = sed + "s/@SERVICE_NAME@/" + service_name + "/g " + stop_file
        execute_command_and_getoutput(sed_cmd)

        sed_cmd = sed + "s/@TARS_APP@/" + self.config.chain_id + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@TARS_SERVER@/" + node_config.node_service.service_name + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)

        # tars config
        sed_cmd = sed + "s/@TARS_LISTEN_IP@/" + node_config.deploy_ip + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@TXPOOL_LISTEN_PORT@/" + str(node_config.tars_listen_port + 0) + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@SCHEDULER_LISTEN_PORT@/" + str(node_config.tars_listen_port + 1) + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@PBFT_LISTEN_PORT@/" + str(node_config.tars_listen_port + 2) + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@LEDGER_LISTEN_PORT@/" + str(node_config.tars_listen_port + 3) + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)
        sed_cmd = sed + "s/@FRONT_LISTEN_PORT@/" + str(node_config.tars_listen_port + 4) + "/g " + conf_file
        execute_command_and_getoutput(sed_cmd)

        if os.path.exists(start_file + ".bak"):
            os.remove(start_file + ".bak")

        if os.path.exists(stop_file + ".bak"):
            os.remove(stop_file + ".bak")
        
        if os.path.exists(conf_file + ".bak"):
            os.remove(conf_file + ".bak")