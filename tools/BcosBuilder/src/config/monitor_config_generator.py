#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities
from common.utilities import ConfigInfo
import os
import shutil


class MonitorConfigGenerator:
    """
    the common Monitor config generator
    """

    def __init__(self, config, node_type):
        self.config = config
        self.node_type = node_type
        self.root_dir = "./generated"
        self.monitor_start_scirpts_file = "start_monitor.sh"
        self.monitor_stop_scirpts_file = "stop_monitor.sh"
        self.monitor_tpl_config = ConfigInfo.monitor_config_tpl_path
        self.prometheus_tpl_config = ConfigInfo.prometheus_config_tpl_path
        self.mtail_tmp_config_file = "node.mtail"
        self.mtail_tmp_path = "mtail/"
        self.mtail_start_scirpts_file = "start_mtail_monitor.sh"
        self.mtail_stop_scirpts_file = "stop_mtail_monitor.sh"
        self.mtail_binary_file = os.path.join(
            ConfigInfo.tpl_binary_path, "mtail")
        self.mtail_src_tpl_config = os.path.join(
            ConfigInfo.tpl_src_mtail_path, self.mtail_tmp_path)

    def generate_all_mtail_config(self, group_config):
        """
        generate all mtail config file
        """
        for node_config in group_config.node_list:
            if self.__generate_and_store_mtail_config(node_config, group_config) is False:
                return False

        return True

    def generate_mtail_config(self):
        """
        generate mtail config for all-node
        """
        for group_config in self.config.group_list.values():
            utilities.print_badage(
                "generate mtail config for group %s" % group_config.group_id)
            if self.generate_all_mtail_config(group_config) is False:
                return False
            utilities.print_badage(
                "generate mtail config for group %s success" % group_config.group_id)
        return True

    def __check_monitor_config(self, node_config):
        if node_config.monitor_listen_port is None:
            raise Exception(
                "the monitor_listen_port for node %s must be set" % node_config.node_service_name)
        if node_config.monitor_log_path is None:
            raise Exception(
                "the monitor_log_path for node %s must be set" % node_config.node_service_name)

    def start_monitor_config(self):
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                self.__check_monitor_config(node_config)
                node_path = self.__get_and_generate_node_log_base_path(
                    node_config)
                mtail_start_scripts_path = os.path.join(
                    node_path, self.mtail_tmp_path, self.mtail_start_scirpts_file)
                if self.node_type == "pro":
                    if utilities.execute_ansible_with_command(mtail_start_scripts_path, node_config.deploy_ip, node_config.monitor_listen_port) is False:
                        return False
                else:
                    for ip in node_config.deploy_ip:
                        if utilities.execute_ansible_with_command(mtail_start_scripts_path, ip, node_config.monitor_listen_port) is False:
                            return False
        monitor_start_scripts_path = os.path.join(
            self.monitor_tpl_config, self.monitor_start_scirpts_file)
        return utilities.execute_monitor_with_command(monitor_start_scripts_path)

    def stop_monitor_config(self):
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                node_path = self.__get_and_generate_node_log_base_path(
                    node_config)
                mtail_start_scripts_path = os.path.join(
                    node_path, self.mtail_tmp_path, self.mtail_stop_scirpts_file)
                if self.node_type == "pro":
                    if utilities.execute_ansible_with_command(mtail_start_scripts_path, node_config.deploy_ip, node_config.monitor_listen_port) is False:
                        return False
                else:
                    for ip in node_config.deploy_ip:
                        if utilities.execute_ansible_with_command(mtail_start_scripts_path, ip, node_config.monitor_listen_port) is False:
                            return False
        monitor_stop_scripts_path = os.path.join(
            self.monitor_tpl_config, self.monitor_stop_scirpts_file)
        return utilities.execute_monitor_with_command(monitor_stop_scripts_path)

    def generate_monitor_config(self):
        """
        generate graphna&prometheus config for all-node 
        """
        utilities.print_badage(" generate graphna&prometheus config ")
        targets = ""
        for group_config in self.config.group_list.values():
            for node_config in group_config.node_list:
                self.__check_monitor_config(node_config)
                if self.node_type == "pro":
                    if targets == "":
                        targets = '"' + node_config.deploy_ip + ':' + \
                            node_config.monitor_listen_port + '"'
                    else:
                        targets += ',"' + node_config.deploy_ip + \
                            ':' + node_config.monitor_listen_port + '"'
                else:
                    for ip in node_config.deploy_ip:
                        if targets == "":
                            targets = '"' + ip + ':' + node_config.monitor_listen_port + '"'
                        else:
                            targets += ',"' + ip + ':' + node_config.monitor_listen_port + '"'
        if self.__generate_and_store_monitor_config(targets) is False:
            return False
        utilities.print_badage(
            "generate graphna&prometheus config success")
        return True

    def __generate_mtail_config(self, mtail_config_path, node_config):
        """
        generate node config: config.ini.tmp
        """
        mtail_config = ""
        with open(mtail_config_path, 'r', encoding='utf-8') as f:
            # mtail_config = f.read()
            for line in f.readlines():
                if(line.find('group') == 0):
                    line = 'group="%s' % (node_config.group_id,) + '"\n'
                if(line.find('node') == 0):
                    line = 'node="%s' % (node_config.node_name,) + '"\n'
                if(line.find('chain') == 0):
                    line = 'chain="%s' % (node_config.chain_id,) + '"\n'
                if(line.find('host') == 0):
                    line = 'host="%s' % (node_config.deploy_ip,) + '"\n'
                mtail_config += line
        return mtail_config

    def store_mtail_config(self, config_content, config_type, config_path, desc):
        """
        store the generated genesis config content for given node
        """
        utilities.log_info("* store %s config for %s\n\t path: %s" %
                           (config_type, desc, config_path))
        with open(config_path, 'w') as configFile:
            configFile.write(config_content)
            utilities.log_info("* store %s config for %s success" %
                               (config_type, desc))
        return True

    def __get_and_generate_node_log_base_path(self, node_config):
        path = os.path.join(node_config.monitor_log_path, node_config.agency_config.chain_id,
                            node_config.node_service.service_name)
        return path

    def __generate_and_store_mtail_config(self, node_config, group_config):
        """
        generate and store mtatil config for given node
        """
        node_log_path = self.__get_and_generate_node_log_base_path(node_config)
        mtail_target_tpl_config = os.path.join(
            node_log_path, self.mtail_tmp_path)
        shutil.copytree(self.mtail_src_tpl_config, mtail_target_tpl_config)
        shutil.copy(self.mtail_binary_file, mtail_target_tpl_config)
        mtail_config_path = os.path.join(
            node_log_path, self.mtail_tmp_path, self.mtail_tmp_config_file)
        config_content = self.__generate_mtail_config(
            mtail_config_path, node_config)
        if self.store_mtail_config(config_content, "mtail", mtail_config_path, node_config.node_service.service_name) is False:
            return False

        mtail_start_scripts_path = os.path.join(
            node_log_path, self.mtail_tmp_path, self.mtail_start_scirpts_file)
        mtail_ansible_src_tpl_config = os.path.join(node_log_path, "mtail")
        mtail_ansible_dest_tpl_config = os.path.join(node_log_path)
        if self.node_type == "pro":
            if utilities.execute_ansible_copy_with_command(node_config.deploy_ip, mtail_ansible_src_tpl_config, mtail_ansible_dest_tpl_config) is False:
                return False
            return utilities.execute_ansible_with_command(mtail_start_scripts_path, node_config.deploy_ip, node_config.monitor_listen_port)
        else:
            for ip in node_config.deploy_ip:
                if utilities.execute_ansible_copy_with_command(ip, mtail_ansible_src_tpl_config, mtail_ansible_dest_tpl_config) is False:
                    return False
                if utilities.execute_ansible_with_command(mtail_start_scripts_path, ip, node_config.monitor_listen_port) is False:
                    return False
        return True

    def __generate_monitor_config(self, targets):
        """
        generate node config: config.ini.tmp
        """
        monitor_config = ""
        with open(self.prometheus_tpl_config, 'r', encoding='utf-8') as f:
            for line in f.readlines():
                if(line.find('      - targets: [') == 0):
                    line = line.replace(
                        '      - targets: [',  '      - targets: [' + targets)
                monitor_config += line
        return monitor_config

    def store_monitor_config(self, config_content, config_type, config_path):
        """
        store the generated genesis config content for given node
        """
        utilities.log_info("* store %s config \n\t path: %s" %
                           (config_type, config_path))
        with open(config_path, "w", encoding="utf-8") as f2:
            f2.write(config_content)
            utilities.log_info("* store %s config success" %
                               (config_type))
        return True

    def __generate_and_store_monitor_config(self, targets):
        """
        generate and store graphna&prometheus config for monitor
        """
        monitor_config_content = self.__generate_monitor_config(targets)
        if self.store_monitor_config(monitor_config_content, "monitor", self.prometheus_tpl_config) is False:
            return False

        monitor_start_scripts_path = os.path.join(
            self.monitor_tpl_config, self.monitor_start_scirpts_file)
        return utilities.execute_monitor_with_command(monitor_start_scripts_path)
