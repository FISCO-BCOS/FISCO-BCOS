#!/usr/bin/python
# -*- coding: UTF-8 -*-

from service.tars_service import TarsService
from common import utilities
from config.node_config_generator import NodeConfigGenerator
import time


class NodeController:
    """
    the node controller
    """

    def __init__(self, config):
        self.config = config
        self.node_config_generator = NodeConfigGenerator(config)

    def generate_all_config(self):
        nodeid_list = self.node_config_generator.generate_all_nodes_pem()
        for node_config in self.config.group_config.node_list:
            if node_config.rpc_service_name is None or len(node_config.rpc_service_name) == 0:
                utilities.log_error(
                    "Must set rpc_service_name when generate or deploy node")
                return False
            if node_config.gateway_service_name is None or len(node_config.gateway_service_name) == 0:
                utilities.log_error(
                    "Must set gateway_service_name when generate or deploy node")
                return False
            if self.node_config_generator.generate_node_all_config(
                    node_config, nodeid_list) is False:
                return False
        return True

    def generate_and_deploy_group_services(self):
        utilities.log_info("generate config for chain = %s, group = %s" % (
            self.config.group_config.chain_id, self.config.group_config.group_id))
        if self.generate_all_config() is False:
            return False
        if self.deploy_group_services() is False:
            return False
        return True

    def deploy_group_services(self):
        utilities.log_info("deploy services for all the group nodes")
        for node_config in self.config.group_config.node_list:
            if self.deploy_node_services(node_config) is False:
                return False
        return True

    def __get_service_list(self):
        services = []
        org_services = []
        for node in self.config.group_config.node_list:
            service_list = node.nodes_service_name_list
            for key in service_list.keys():
                service_mapping = service_list[key]
                for service_name in service_mapping.keys():
                    services.append(service_name)
                    org_services.append(service_mapping[service_name])
        return (services, org_services)

    def start_group(self):
        (service_list, _) = self.__get_service_list()
        return self.__start_all(service_list)

    def stop_group(self):
        (service_list, _) = self.__get_service_list()
        return self.__stop_all(service_list)

    def undeploy_group(self):
        (service_list, _) = self.__get_service_list()
        return self.__undeploy_all(service_list)

    def __start_all(self, service_list):
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in service_list:
            if tars_service_obj.restart_server(service) is False:
                utilities.log_error("start node %s failed" % service)
                return False
            else:
                utilities.log_info("start node %s success" % service)
        return True

    def __stop_all(self, service_list):
        ret = True
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in service_list:
            if tars_service_obj.stop_server(service) is False:
                utilities.log_error("stop node %s failed" % service)
                ret = False
            else:
                utilities.log_info("stop node %s success" % service)
        return ret

    def __undeploy_all(self, service_list):
        ret = True
        utilities.log_info("undeploy services for all the group nodes")
        for node_config in self.config.group_config.node_list:
            if self.__undeploy_service(node_config) is False:
                ret = False
        return ret

    def __undeploy_service(self, node_config):
        service_list = node_config.nodes_service_name_list
        for key in service_list.keys():
            service_mapping = service_list[key]
            for service_name in service_mapping.keys():
                utilities.log_info("undeploy service %s" % service_name)
                org_service_name = service_mapping[service_name]
                deploy_ip = node_config.service_list[org_service_name]
                tars_service_obj = TarsService(
                    self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
                ret = tars_service_obj.undeploy_tars(service_name)
                if ret is False:
                    utilities.log_error(
                        "undeploy service %s failed" % service_name)
                else:
                    utilities.log_info(
                        "undeploy service %s success" % service_name)
        return True

    def upgrade_group(self):
        utilities.log_info("upgrade services for all the group nodes")
        for node_config in self.config.group_config.node_list:
            utilities.log_info("upgrade service for node %s" %
                               node_config.node_name)
            ret = self.__upgrade_node_service(node_config)
            if ret is False:
                utilities.log_error(
                    "upgrade service for node %s failed" % node_config.node_name)
                return False
        return True

    def __upgrade_node_service(self, node_config):
        service_list = node_config.nodes_service_name_list
        for key in service_list.keys():
            service_mapping = service_list[key]
            for service_name in service_mapping.keys():
                utilities.log_info("upgrade service %s" % service_name)
                org_service_name = service_mapping[service_name]
                if self.__upgrade_service(
                        node_config, service_name, org_service_name) is False:
                    return False
        return True

    def __upgrade_service(self, node_config, service_name, org_service_name):
        deploy_ip = node_config.service_list[org_service_name]
        tars_service_obj = TarsService(
            self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        (ret, patch_id) = self.__upload_package(
            tars_service_obj, service_name, org_service_name)
        if ret is False:
            return False
        # patch tars
        (ret, server_id) = tars_service_obj.get_server_id(service_name, deploy_ip)
        if ret is False:
            return False
        if tars_service_obj.patch_tars(server_id, patch_id) is False:
            return False
        return True

    def deploy_node_services(self, node_config):
        service_list = node_config.nodes_service_name_list
        for key in service_list.keys():
            service_mapping = service_list[key]
            for service_name in service_mapping.keys():
                utilities.log_info("deploy service %s" % service_name)
                org_service_name = service_mapping[service_name]
                if self.__deploy_service(
                        node_config, service_name, org_service_name) is False:
                    utilities.log_error(
                        "deploy service %s failed" % service_name)
                    return False
                time.sleep(5)
        return True

    def __deploy_service(self, node_config, service_name, org_service_name):
        deploy_ip = node_config.service_list[org_service_name]
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        # create application
        tars_service_obj.create_application()
        # create service
        obj_name = org_service_name + "Obj"
        obj_list = []
        obj_list = node_config.obj_name_list
        ret = tars_service_obj.deploy_single_service(
            service_name, obj_list, False)
        if ret is False:
            return False
        # upload_package
        (ret, patch_id) = self.__upload_package(
            tars_service_obj, service_name, org_service_name)
        if ret is False:
            return False
        # add configuration
        (config_file_list, config_path_list) = self.node_config_generator.get_all_service_info(
            node_config, service_name)
        ret = tars_service_obj.add_node_config_list(
            config_file_list, service_name, config_path_list)
        if ret is False:
            return False
        # patch tars
        (ret, server_id) = tars_service_obj.get_server_id(service_name, deploy_ip)
        if ret is False:
            return False
        return tars_service_obj.patch_tars(server_id, patch_id)

    def __upload_package(self, tars_service, service_name, org_service_name):
        # upload the package
        (ret, package_path) = utilities.try_to_rename_tgz_package("generated",
                                                                  self.config.tars_config.tars_pkg_dir, service_name, org_service_name)
        if ret is False:
            utilities.log_error(
                "upload package for service %s failed" % service_name)
            return (False, -1)
        return tars_service.upload_tars_package(service_name, package_path)

    def generate_all_expand_config(self):
        for node_config in self.config.group_config.node_list:
            if self.__generate_expand_node_config(node_config) is False:
                return False
        # generate pem files
        ret = self.node_config_generator.generate_all_nodes_pem()
        if len(ret) == 0:
            utilities.log_error("generate the expand config failed")
            return False
        return True

    def __generate_expand_node_config(self, node_config):
        if node_config.expanded_service is None or len(node_config.expanded_service) == 0:
            utilities.log_error(
                "must set the expanded_service when expand nodes, e.g. groupnode00BcosNodeService")
            return False
        return self.node_config_generator.generate_expand_node_config(node_config)

    def expand_and_deploy_all_nodes(self):
        for node_config in self.config.group_config.node_list:
            if self.__generate_expand_node_config(node_config) is False:
                return False
            # generate pem files
            ret = self.node_config_generator.generate_all_nodes_pem()
            if len(ret) == 0:
                utilities.log_error("generate the expand config failed")
                return False
            # generate pem files
            if self.deploy_node_services(node_config) is False:
                return False
        return True
