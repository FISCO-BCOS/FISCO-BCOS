#!/usr/bin/python
# -*- coding: UTF-8 -*-

from config.service_config_generator import ServiceConfigGenerator
from service.tars_service import TarsService
from common.utilities import ServiceInfo
from common import utilities


class ServiceController:
    """
    common controller for rpc/gateway
    """

    def __init__(self, config, service_type, node_type):
        self.config = config
        self.service_type = service_type
        self.service_list = self.config.rpc_service_list
        self.node_type = node_type
        self.config_generator = ServiceConfigGenerator(
            self.config, self.service_type, self.node_type)
        if self.service_type == ServiceInfo.gateway_service_type:
            self.service_list = self.config.gateway_service_list

    def deploy_all(self):
        for service in self.service_list.values():
            if self.__deploy_service(service) is False:
                utilities.log_error("deploy service %s failed" % service.name)
                return False
        return True

    def stop_all(self):
        ret = True
        for service in self.service_list.values():
            if self.__stop_service(service) is False:
                ret = False
                utilities.log_error("stop service %s failed" % service.name)
            else:
                utilities.log_info("stop service %s success" % service.name)
        return ret

    def start_all(self):
        for service in self.service_list.values():
            if self.__start_service(service) is False:
                utilities.log_error("start service %s failed" % service.name)
                return False
            else:
                utilities.log_info("start service %s success" % service.name)
        return True

    def undeploy_all(self):
        ret = True
        for service in self.service_list.values():
            if self.__undeploy_service(service) is False:
                ret = False
                utilities.log_error(
                    "undeploy service %s failed" % service.name)
            else:
                utilities.log_info(
                    "undeploy service %s success" % service.name)
        return ret

    def upgrade_all(self):
        for service in self.service_list.values():
            if self.__upgrade_service(service) is False:
                utilities.log_error("upgrade service %s failed" % service.name)
                return False
            else:
                utilities.log_info("upgrade service %s success" % service.name)
        return True

    def gen_all_service_config(self):
        if self.config_generator.generate_all_config() is False:
            utilities.log_error(
                "gen configuration for %s service failed" % self.service_type)
            return False
        return True

    def expand_all(self):
        for service in self.service_list.values():
            if service.expanded_ip is None or len(service.expanded_ip) == 0:
                utilities.log_error("Must set expanded_ip when expand service, service name: %s, deploy ip: %s, type: %s" % (
                    service, service.deploy_ip_list, self.service_type))
                return False
            if self.__expand_service_list(service) is False:
                utilities.log_error("expand service %s to %s failed, type: %s!" % (
                    service.name, service.deploy_ip_list, self.service_type))
                return False
        return True

    def __deploy_service(self, service_config):
        if len(service_config.deploy_ip_list) == 0:
            utilities.log_info("No service to deploy")
        for deploy_ip in service_config.deploy_ip_list:
            utilities.log_info("deploy_service to %s, app: %s, name: %s" % (
                deploy_ip, self.config.chain_id, service_config.name))
            if self.__deploy_service_to_given_ip(service_config, deploy_ip) is False:
                return False
        return True

    def __expand_service_list(self, service_config):
        for ip in service_config.deploy_ip_list:
            utilities.log_info("expand to %s, app: %s, name: %s" % (
                ip, self.config.chain_id, service_config.name))
            if self.__deploy_service_to_given_ip(service_config, ip) is False:
                return False
        return True

    def __upgrade_service(self, service_config):
        for ip in service_config.deploy_ip_list:
            utilities.log_info("upgrade_service %s to %s" %
                               (service_config.name, ip))
            ret = self.__upgrade_service_to_given_ip(service_config, ip)
            if ret is False:
                return False
        return True

    def __deploy_service_to_given_ip(self, service_config, deploy_ip):
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        # create application
        tars_service.create_application()
        # create the service
        obj_list = service_config.service_obj_list
        # deploy service
        ret = tars_service.deploy_single_service(
            service_config.name, obj_list, True)
        if ret is False:
            return False
        # add configuration files
        (config_file_list, config_path_list) = self.config_generator.get_config_file_list(
            service_config, deploy_ip)
        ret = tars_service.add_config_list(
            config_file_list, service_config.name, deploy_ip, config_path_list, True)
        if ret is False:
            return False
        return self.__upgrade_service_by_config_info(tars_service, service_config)

    def __expand_service_to_given_ip(self, service_config, node_name, expand_node_ip):
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, expand_node_ip)
        # expand the service
        obj_list = service_config.service_obj_list
        expand_node_list = [expand_node_ip]
        ret = tars_service.expand_server_with_preview(
            service_config.name, node_name, expand_node_list, obj_list)
        if ret is False:
            utilities.log_error("expand service failed, app: %s, service: %s, node: %s" % (
                self.config.chain_id, service_config.name, expand_node_ip))
            return False
        # add configuration files
        (config_file_list, config_path_list) = self.config_generator.get_config_file_list(
            service_config, expand_node_ip)
        ret = tars_service.add_config_list(
            config_file_list, service_config.name, expand_node_ip, config_path_list, True)
        if ret is False:
            return False
        # patch the service
        return self.__upgrade_service_by_config_info(tars_service, service_config)

    def __upgrade_service_to_given_ip(self, service_config, deploy_ip):
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        return self.__upgrade_service_by_config_info(tars_service, service_config)

    def __upgrade_service_by_config_info(self, tars_service, service_config):
        # upload package
        (ret, patch_id) = self.__upload_package(
            tars_service, service_config.name, service_config.binary_name)
        if ret is False:
            return False
        # patch tars
        # get the service info
        (ret, server_id) = tars_service.get_server_id(
            service_config.name, tars_service.deploy_ip)
        if ret is False:
            return False
        return tars_service.patch_tars(server_id, patch_id)

    def __undeploy_service(self, service_config):
        for ip in service_config.deploy_ip_list:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            utilities.log_info(
                "undeploy service for node %s, service: %s" % (ip, service_config.name))
            if tars_service.undeploy_tars(service_config.name) is False:
                utilities.log_error(
                    "undeploy service %s for node %s failed" % (ip, service_config.name))
        return True

    def __start_service(self, service_config):
        for ip in service_config.deploy_ip_list:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            if tars_service.restart_server(service_config.name) is False:
                utilities.log_error("start service for node %s failed" % ip)
                return False
        return True

    def __stop_service(self, service_config):
        for ip in service_config.deploy_ip_list:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            utilities.log_info("stop service %s, node: %s" %
                               (service_config.name, ip))
            if tars_service.stop_server(service_config.name) is False:
                utilities.log_error("stop service for node %s failed" % ip)
                return False
        return True

    def __upload_package(self, tars_service, service_name, org_service_name):
        (ret, package_path) = utilities.try_to_rename_tgz_package("generated",
                                                                  self.config.tars_config.tars_pkg_dir, service_name, org_service_name)
        if ret is False:
            utilities.log_error(
                "upload package for service %s failed for rename package name failed" % service_name)
            return (False, -1)
        return tars_service.upload_tars_package(service_name, package_path)
