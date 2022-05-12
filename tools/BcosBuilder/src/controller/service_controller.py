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
        self.service_dict = self.config.rpc_config
        self.node_type = node_type
        if self.service_type == ServiceInfo.gateway_service_type:
            self.service_dict = self.config.gateway_config

    def deploy_all(self):
        for service in self.service_dict.values():
            if self.deploy_service(service) is False:
                utilities.log_error("deploy service %s failed" % service.name)
                return False
        return True

    def stop_all(self):
        ret = True
        for service in self.service_dict.values():
            if self.stop_service(service) is False:
                ret = False
                utilities.log_error("stop service %s failed" % service.name)
            else:
                utilities.log_info("stop service %s success" % service.name)
        return ret

    def start_all(self):
        for service in self.service_dict.values():
            if self.start_service(service) is False:
                utilities.log_error("start service %s failed" % service.name)
                return False
            else:
                utilities.log_info("start service %s success" % service.name)
        return True

    def undeploy_all(self):
        ret = True
        for service in self.service_dict.values():
            if self.undeploy_service(service) is False:
                ret = False
                utilities.log_error(
                    "undeploy service %s failed" % service.name)
            else:
                utilities.log_info(
                    "undeploy service %s success" % service.name)
        return ret

    def upgrade_all(self):
        for service in self.service_dict.values():
            if self.upgrade_service(service) is False:
                utilities.log_error("upgrade service %s failed" % service.name)
                return False
            else:
                utilities.log_info("upgrade service %s success" % service.name)
        return True

    def gen_all_service_config(self):
        for service in self.service_dict.values():
            if self.gen_service_config(service) is False:
                utilities.log_error(
                    "gen configuration for service %s failed" % service.name)
                return False
            else:
                utilities.log_info(
                    "gen configuration for service %s success" % service.name)
        return True

    def gen_service_config(self, service_config):
        for ip in service_config.deploy_ip:
            utilities.log_info(
                "* generate service config for %s : %s" % (ip, service_config.name))
            config_generator = ServiceConfigGenerator(
                self.config, self.service_type, service_config, ip, self.node_type)
            ret = config_generator.generate_all_config()
            if ret is False:
                return ret
        return True

    def deploy_service(self, service_config):
        if len(service_config.deploy_ip) == 0:
            utilities.log_info("No service to deploy")
        deploy_ip = (service_config.deploy_ip)[0]
        utilities.log_info("deploy_service to %s, app: %s, name: %s" % (
            deploy_ip, self.config.chain_id, service_config.name))
        self.deploy_service_to_given_ip(service_config, deploy_ip)
        for i in range(1, len(service_config.deploy_ip)):
            ip = service_config.deploy_ip[i]
            utilities.log_info("expand service to %s, app: %s, name: %s" % (
                ip, self.config.chain_id, service_config.name))
            if self.expand_service_to_given_ip(service_config, deploy_ip, ip) is False:
                return False
        return True

    def expand_all(self):
        for service in self.service_dict.values():
            if service.expanded_ip is None or len(service.expanded_ip) == 0:
                utilities.log_error("Must set expanded_ip when expand service, service name: %s, deploy ip: %s, type: %s" % (
                    service, service.deploy_ip, self.service_type))
                return False
            if self.expand_service_list(service) is False:
                utilities.log_error("expand service %s to %s failed, type: %s!" % (
                    service, service.deploy_ip, self.service_type))
                return False
        return True

    def expand_service_list(self, service_config):
        for ip in service_config.deploy_ip:
            utilities.log_info("expand to %s, app: %s, name: %s" % (
                ip, self.config.chain_id, service_config.name))
            if self.expand_service_to_given_ip(service_config, service_config.expanded_ip, ip) is False:
                return False
        return True

    def upgrade_service(self, service_config):
        for ip in service_config.deploy_ip:
            utilities.log_info("upgrade_service %s to %s" %
                               (service_config.name, ip))
            ret = self.upgrade_service_to_given_ip(service_config, ip)
            if ret is False:
                return False
        return True

    def deploy_service_to_given_ip(self, service_config, deploy_ip):
        config_generator = ServiceConfigGenerator(
            self.config, self.service_type, service_config, deploy_ip, self.node_type)
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        # create application
        tars_service.create_application()
        # create the service
        obj_name = self.get_service_obj()
        obj_list = [obj_name]
        # deploy service
        ret = tars_service.deploy_single_service(
            service_config.name, obj_list, True)
        if ret is False:
            return False
        # add configuration files
        ret = tars_service.add_config_list(
            config_generator.config_file_list, service_config.name, deploy_ip, config_generator.config_path_list, True)
        if ret is False:
            return False
        org_service_name = self.get_org_service_name()
        return self.upgrade_service_by_config_info(tars_service, service_config, org_service_name, config_generator)

    def expand_service_to_given_ip(self, service_config, node_name, expand_node_ip):
        config_generator = ServiceConfigGenerator(
            self.config, self.service_type, service_config, expand_node_ip, self.node_type)
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, expand_node_ip)
        # expand the service
        obj_name = self.get_service_obj()
        obj_list = [obj_name]
        expand_node_list = [expand_node_ip]
        ret = tars_service.expand_server_with_preview(
            service_config.name, node_name, expand_node_list, obj_list)
        if ret is False:
            utilities.log_error("expand service failed, app: %s, service: %s, node: %s" % (
                self.config.chain_id, service_config.name, expand_node_ip))
            return False
        # add configuration files
        ret = tars_service.add_config_list(
            config_generator.config_file_list, service_config.name, expand_node_ip, config_generator.config_path_list, True)
        if ret is False:
            return False
        # patch the service
        org_service_name = self.get_org_service_name()
        return self.upgrade_service_by_config_info(tars_service, service_config, org_service_name, config_generator)

    def get_org_service_name(self):
        org_service_name = ServiceInfo.gateway_service
        if self.service_type == ServiceInfo.rpc_service_type:
            org_service_name = ServiceInfo.rpc_service
        return org_service_name

    def get_service_obj(self):
        org_service_obj = ServiceInfo.gateway_service_obj
        if self.service_type == ServiceInfo.rpc_service_type:
            org_service_obj = ServiceInfo.rpc_service_obj
        return org_service_obj

    def upgrade_service_to_given_ip(self, service_config, deploy_ip):
        config_generator = ServiceConfigGenerator(
            self.config, self.service_type, service_config, deploy_ip, self.node_type)
        tars_service = TarsService(self.config.tars_config.tars_url,
                                   self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        return self.upgrade_service_by_config_info(tars_service, service_config, self.get_org_service_name(), config_generator)

    def upgrade_service_by_config_info(self, tars_service, service_config, org_service_name, config_generator):
        # upload package
        (ret, patch_id) = self.upload_package(
            tars_service, service_config.name, org_service_name)
        if ret is False:
            return False
        # patch tars
        # get the service info
        (ret, server_id) = tars_service.get_server_id(
            service_config.name, tars_service.deploy_ip)
        if ret is False:
            return False
        return tars_service.patch_tars(server_id, patch_id)

    def undeploy_service(self, service_config):
        for ip in service_config.deploy_ip:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            utilities.log_info(
                "undeploy service for node %s, service: %s" % (ip, service_config.name))
            if tars_service.undeploy_tars(service_config.name) is False:
                utilities.log_error(
                    "undeploy service %s for node %s failed" % (ip, service_config.name))
        return True

    def start_service(self, service_config):
        for ip in service_config.deploy_ip:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            if tars_service.restart_server(service_config.name) is False:
                utilities.log_error("start service for node %s failed" % ip)
                return False
        return True

    def stop_service(self, service_config):
        for ip in service_config.deploy_ip:
            tars_service = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, ip)
            utilities.log_info("stop service %s, node: %s" %
                               (service_config.name, ip))
            if tars_service.stop_server(service_config.name) is False:
                utilities.log_error("stop service for node %s failed" % ip)
                return False
        return True

    def upload_package(self, tars_service, service_name, org_service_name):
        (ret, package_path) = utilities.try_to_rename_tgz_package("generated",
                                                                  self.config.tars_config.tars_pkg_dir, service_name, org_service_name)
        if ret is False:
            utilities.log_error(
                "upload package for service %s failed for rename package name failed" % service_name)
            return (False, -1)
        return tars_service.upload_tars_package(service_name, package_path)
