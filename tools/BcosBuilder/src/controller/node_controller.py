#!/usr/bin/python
# -*- coding: UTF-8 -*-
from config.max_node_config_generator import MaxNodeConfigGenerator
from config.node_config_generator import NodeConfigGenerator
from common import utilities
from service.tars_service import TarsService


class NodeController:
    """
    the max node controller
    """

    def __init__(self, config, node_type):
        self.config = config
        if node_type == "max":
            self.node_generator = MaxNodeConfigGenerator(
                self.config, node_type)
        else:
            self.node_generator = NodeConfigGenerator(self.config, node_type)

    def generate_all_config(self):
        return self.node_generator.generate_all_config(False)

    def deploy_group_services(self):
        """
        deploy max node for all group
        """
        for node in self.config.node_list.values():
            if self.__deploy_all_service(node) is False:
                return False
        return True

    def upgrade_group(self):
        utilities.log_info("upgrade services for all the group nodes")
        for node in self.config.node_list.values():
            utilities.log_info("upgrade service for node %s" %
                               node.node_name)
            if self.__upgrade_all_service(node) is False:
                return False
        return True

    def stop_group(self):
        for node in self.config.node_list.values():
            if self.__stop_all(node) is False:
                return False
        return True

    def start_group(self):
        for node in self.config.node_list.values():
            if self.__start_all(node) is False:
                return False
        return True

    def generate_and_deploy_group_services(self):
        if self.generate_all_config() is False:
            return False
        if self.deploy_group_services() is False:
            return False
        return True

    def undeploy_group(self):
        utilities.log_info("undeploy services for all the group nodes")
        for node_config in self.config.node_list.values():
            for service in node_config.service_list:
                if self.__undeploy_service(service) is False:
                    return False
        return True

    def generate_all_expand_config(self):
        """
        generate expand config
        """
        if self.node_generator.generate_all_config(True) is False:
            return False
        return True

    def expand_and_deploy_all_nodes(self):
        """
        expand and deploy all nodes
        """
        if self.generate_all_expand_config() is False:
            return False
        if self.deploy_group_services() is False:
            return False
        return True

    def __start_all(self, node):
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in node.service_list:
            if tars_service_obj.restart_server(service.service_name) is False:
                utilities.log_error("start node %s failed" %
                                    service.service_name)
                return False
            else:
                utilities.log_info("start node %s success" %
                                   service.service_name)
        return True

    def __stop_all(self, node):
        ret = True
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in node.service_list:
            if tars_service_obj.stop_server(service.service_name) is False:
                utilities.log_error("stop node %s failed" %
                                    service.service_name)
                ret = False
            else:
                utilities.log_info("stop node %s success" %
                                   service.service_name)
        return ret

    def __undeploy_service(self, node_service_config):
        for ip in node_service_config.deploy_ip_list:
            utilities.log_info("undeploy service %s from %s" %
                               (node_service_config.service_name, ip))
            tars_service_obj = TarsService(
                self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, ip)
            ret = tars_service_obj.undeploy_tars(
                node_service_config.service_name)
            if ret is False:
                utilities.log_error("undeploy service %s from %s failed" % (
                    node_service_config.service_name, ip))
            else:
                utilities.log_info("undeploy service %s from %s success" % (
                    node_service_config.service_name, ip))
        return True

    def __upgrade_all_service(self, node_config):
        for service in node_config.service_list:
            for ip in service.deploy_ip_list:
                tars_service_obj = TarsService(
                    self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, ip)
                if self.__upgrade_service(ip, tars_service_obj, node_config, service) is False:
                    return False
        return True

    def __deploy_all_service(self, node_config):
        for service in node_config.service_list:
            for ip in service.deploy_ip_list:
                if self.__deploy_service(ip, node_config, service) is False:
                    return False
        return True

    def __deploy_service(self, deploy_ip, node_config, node_service_config):
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        # create application
        tars_service_obj.create_application()
        # create service
        if tars_service_obj.deploy_single_service(node_service_config.service_name, node_service_config.service_obj_list, True) is False:
            return False
        return self.__upgrade_service(deploy_ip, tars_service_obj, node_config, node_service_config)

    def __upgrade_service(self, deploy_ip, tars_service_obj, node_config, node_service_config):
        # upload package
        (ret, patch_id) = self.__upload_package(
            tars_service_obj, node_service_config)
        if ret is False:
            return False
        # add configuration
        config_path_list = self.node_generator.get_config_file_path_list(
            node_service_config, node_config)
        ret = tars_service_obj.add_node_config_list(
            deploy_ip, node_service_config.config_file_list, node_service_config.service_name, config_path_list)
        if ret is False:
            return False
        # patch tars
        (ret, server_id) = tars_service_obj.get_server_id(
            node_service_config.service_name, deploy_ip)
        if ret is False:
            return False
        return tars_service_obj.patch_tars(server_id, patch_id)

    def __upload_package(self, tars_service, node_service_config):
        """
        upload package
        """
        (ret, package_path) = utilities.try_to_rename_tgz_package("generated",
                                                                  self.config.tars_config.tars_pkg_dir, node_service_config.service_name, node_service_config.base_service_name)
        if ret is False:
            utilities.log_error(
                "upload package for service %s failed" % node_service_config.service_name)
            return (False, -1)
        return tars_service.upload_tars_package(node_service_config.service_name, package_path)
