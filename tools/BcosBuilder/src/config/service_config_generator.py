#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
from common import utilities
from common.utilities import ServiceInfo
from common.utilities import ConfigInfo
import json
import os
import uuid


class ServiceConfigGenerator:
    def __init__(self, config, service_type, service_config, deploy_ip):
        self.config = config
        self.ini_file = "config.ini"
        self.ini_tmp_file = "config.ini.tmp"
        self.network_file = "nodes.json"
        self.network_tmp_file = "nodes.json.tmp"
        self.service_type = service_type
        self.service_config = service_config
        self.deploy_ip = deploy_ip
        self.__init(service_type)

    def __init(self, service_type):
        self.config_file_list = []
        self.config_path_list = []
        if service_type == ServiceInfo.rpc_service_type:
            self.root_dir = "generated/rpc"
            self.section = "rpc"
            self.tpl_config_path = ConfigInfo.rpc_config_tpl_path
            self.sm_ssl = self.config.rpc_sm_ssl
            if len(self.config.rpc_ca_cert_path) == 0:
                self.config.rpc_ca_cert_path = self.get_ca_cert_dir()
            self.ca_cert_path = self.config.rpc_ca_cert_path
        if service_type == ServiceInfo.gateway_service_type:
            self.root_dir = "generated/gateway"
            self.section = "p2p"
            self.tpl_config_path = ConfigInfo.gateway_config_tpl_path
            (connect_file, connect_file_path) = self.get_network_connection_config_info()
            self.sm_ssl = self.config.gateway_sm_ssl
            self.config_file_list.append(connect_file)
            self.config_path_list.append(connect_file_path)
            if len(self.config.gateway_ca_cert_path) == 0:
                self.config.gateway_ca_cert_path = self.get_ca_cert_dir()
            self.ca_cert_path = self.config.gateway_ca_cert_path
        (file_list, path_list) = self.get_cert_config_info()
        self.config_file_list.extend(file_list)
        self.config_path_list.extend(path_list)

        (ini_file, ini_path) = self.get_ini_config_info()
        self.config_file_list.append(ini_file)
        self.config_path_list.append(ini_path)

    def generate_all_config(self):
        if self.service_type == ServiceInfo.gateway_service_type:
            return self.generate_gateway_config_files()
        else:
            return self.generate_rpc_config_files()

    def generate_rpc_config_files(self):
        utilities.log_info("* generate config for the rpc service")
        if self.generate_ini_config() is False:
            return False
        if self.generate_cert() is False:
            return False
        utilities.log_info("* generate config for the rpc service success")
        return True

    def generate_gateway_config_files(self):
        utilities.log_info("* generate config for the gateway service")
        if self.generate_ini_config() is False:
            return False
        if self.generate_cert() is False:
            return False
        if self.generate_gateway_connection_info() is False:
            return False
        utilities.log_info("* generate config for the gateway service success")
        return True

    def get_ini_config_info(self):
        config_path = os.path.join(
            self.root_dir, self.config.chain_id, self.deploy_ip, self.service_config.name, self.ini_tmp_file)
        return (self.ini_file, config_path)

    def get_network_connection_config_info(self):
        config_path = os.path.join(self.root_dir, self.config.chain_id, self.deploy_ip,
                                   self.service_config.name, self.network_tmp_file)
        return (self.network_file, config_path)

    def get_cert_output_dir(self):
        return os.path.join(self.root_dir, self.config.chain_id, self.deploy_ip, self.service_config.name)

    def get_ca_cert_dir(self):
        return os.path.join(self.root_dir, self.config.chain_id, "ca")

    def generate_ini_config(self):
        """
        generate config.ini.tmp
        """
        (_, generated_file_path) = self.get_ini_config_info()
        if os.path.exists(generated_file_path) is True:
            utilities.log_error(
                "config file %s already exists, please delete after confirming carefully" % generated_file_path)
            return False
        ini_config = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        ini_config.read(self.tpl_config_path)
        ini_config[self.section]['listen_ip'] = self.service_config.listen_ip
        ini_config[self.section]['listen_port'] = str(
            self.service_config.listen_port)
        ini_config[self.section]['sm_ssl'] = utilities.convert_bool_to_str(
            self.sm_ssl)
        ini_config[self.section]['thread_count'] = str(
            self.service_config.thread_count)
        ini_config["service"]['gateway'] = self.config.chain_id + \
            "." + self.service_config.gateway_service_name
        ini_config["service"]['rpc'] = self.config.chain_id + \
            "." + self.service_config.rpc_service_name
        ini_config["chain"]['chain_id'] = self.config.chain_id

        # generate failover config
        failover_section = "failover"
        ini_config[failover_section]["enable"] = utilities.convert_bool_to_str(
            self.config.enable_failover)
        ini_config[failover_section]["cluster_url"] = self.config.failover_cluster_url

        # generate uuid according to chain_id and gateway_service_name
        uuid_name = ini_config["service"]['gateway']
        # TODO: Differentiate between different agency
        ini_config[self.section]['uuid'] = str(
            uuid.uuid3(uuid.NAMESPACE_URL, uuid_name))
        utilities.mkfiledir(generated_file_path)
        with open(generated_file_path, 'w') as configfile:
            ini_config.write(configfile)
        utilities.log_info("* generate %s" % generated_file_path)
        return True

    def generate_gateway_connection_info(self):
        (_, generated_file_path) = self.get_network_connection_config_info()
        if os.path.exists(generated_file_path):
            utilities.log_error(
                "config file %s already exists, please delete after confirming carefully" % generated_file_path)
            return False
        peers = {}
        peers["nodes"] = self.service_config.peers
        utilities.mkfiledir(generated_file_path)
        with open(generated_file_path, 'w') as configfile:
            json.dump(peers, configfile)
        utilities.log_info(
            "* generate gateway connection file: %s" % generated_file_path)
        return True

    def ca_generated(self):
        if self.sm_ssl is False:
            if os.path.exists(os.path.join(self.ca_cert_path, "ca.crt")) and os.path.exists(os.path.join(self.ca_cert_path, "ca.key")):
                return True
        else:
            if os.path.exists(os.path.join(self.ca_cert_path, "sm_ca.crt")) and os.path.exists(os.path.join(self.ca_cert_path, "sm_ca.key")):
                return True
        return False

    def generate_cert(self):
        output_dir = self.get_cert_output_dir()
        if self.ca_generated() is False:
            # generate the ca cert
            utilities.generate_ca_cert(self.sm_ssl, self.ca_cert_path)
        utilities.log_info("* generate cert, output path: %s" % (output_dir))
        utilities.generate_node_cert(
            self.sm_ssl, self.ca_cert_path, output_dir)
        if self.service_type == ServiceInfo.rpc_service_type:
            utilities.log_info(
                "* generate sdk cert, output path: %s" % (output_dir))
            utilities.generate_sdk_cert(
                self.sm_ssl, self.ca_cert_path, output_dir)
        return True

    def get_cert_config_info(self):
        ssl_config_files = ServiceInfo.ssl_file_list
        if self.sm_ssl is True:
            ssl_config_files = ServiceInfo.sm_ssl_file_list
        generated_file_paths = []
        for item in ssl_config_files:
            output_dir = self.get_cert_output_dir()
            generated_file_paths.append(os.path.join(output_dir, "ssl", item))
        return (ssl_config_files, generated_file_paths)
