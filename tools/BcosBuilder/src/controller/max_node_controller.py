#!/usr/bin/python
# -*- coding: UTF-8 -*-
from config.max_node_config_generator import MaxNodeConfigGenerator


class MaxNodeController:
    """
    the max node controller
    """

    def __init__(self, config):
        self.config = config
        self.max_node_generator = MaxNodeConfigGenerator(self.config)

    def generate_all_config(self):
        return self.max_node_generator.generate_all_config()

    def deploy_group_services(self):
        """
        deploy max node for all group
        """
        for node in self.max_node_list:
            if self.__deploy_all_service(node) is False:
                return False
        return True

    def upgrade_group(self):
        utilities.log_info("upgrade services for all the group nodes")
        for max_node in self.config.group_config.max_node_list:
            utilities.log_info("upgrade service for node %s" %
                               max_node.node_name)
            ret = self.__upgrade_all_service(max_node)
        return

    def stop_group(self):
        for max_node in self.config.group_config.max_node_list:
            if self.__stop_all(max_node) is False:
                return False
        return True

    def start_group(self):
        for max_node in self.config.group_config.max_node_list:
            if self.__start_all(max_node) is False:
                return False
        return True

    def generate_and_deploy_group_services(self):
        if self.generate_all_config() is False:
            return False
        if self.deploy_group_services is False:
            return False
        return True

    def undeploy_group(self):
        utilities.log_info("undeploy services for all the group nodes")
        for max_node_config in self.config.group_config.max_node_list:
            for service in max_node_config.service_list:
                if self.__undeploy_service(service) is False:
                    return False
        return True

    def generate_all_expand_config(self):
        return True

    def expand_and_deploy_all_nodes(self):
        return True

    def __start_all(self, max_node):
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in max_node.service_list:
            if tars_service_obj.restart_server(service.service_name) is False:
                utilities.log_error("start node %s failed" %
                                    service.service_name)
                return False
            else:
                utilities.log_info("start node %s success" %
                                   service.service_name)
        return True

    def __stop_all(self, max_node):
        ret = True
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, "")
        for service in max_node.service_list:
            if tars_service_obj.stop_server(service.service_name) is False:
                utilities.log_error("stop node %s failed" %
                                    service.service_name)
                ret = False
            else:
                utilities.log_info("stop node %s success" %
                                   service.service_name)
        return ret

    def __undeploy_service(self, max_node_service_config):
        for ip in max_node_service_config.deploy_ip_list:
            utilities.log_info("undeploy service %s from %s" %
                               (max_node_service_config.service_name, ip))
            tars_service_obj = TarsService(
                self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, ip)
            ret = tars_service_obj.undeploy_tars(
                max_node_service_config.service_name)
            if ret is False:
                utilities.log_error("undeploy service %s from %s failed" % (
                    max_node_service_config.service_name, ip))
            else:
                utilities.log_info("undeploy service %s from %s success" % (
                    max_node_service_config.service_name, ip))
        return True

    def __upgrade_all_service(self, max_node_config):
        for service in max_node_config.service_list:
            for ip in service.deploy_ip_list:
                tars_service_obj = TarsService(
                    self.config.tars_config.tars_url, self.config.tars_config.tars_token, self.config.chain_id, ip)
                if self.__upgrade_service(ip, tars_service_obj, max_node_config, service) is False:
                    return False
        return True

    def __deploy_all_service(self, max_node_config):
        for service in max_node_config.service_list:
            for ip in service.deploy_ip_list:
                if self.__deploy_service(ip, max_node_config, service) is False:
                    return False
        return True

    def __deploy_service(self, deploy_ip, max_node_config, max_node_service_config):
        tars_service_obj = TarsService(self.config.tars_config.tars_url,
                                       self.config.tars_config.tars_token, self.config.chain_id, deploy_ip)
        # create application
        tars_service_obj.create_application()
        # create service
        if tars_service_obj.deploy_single_service(max_node_service_config.service_name, max_node_service_config.service_obj_list, True) is False:
            return False
        return self.__upgrade_service(deploy_ip, tars_service_obj, max_node_config, max_node_service_config)

    def __upgrade_service(self, deploy_ip, tars_service_obj, max_node_config, max_node_service_config):
        # upload package
        (ret, patch_id) = self.__upload_package(
            tars_service_obj, max_node_service_config)
        if ret is False:
            return False
        # add configuration
        config_path_list = self.max_node_generator.get_config_file_path_list(
            max_node_service_config, max_node_config)
        ret = tars_service_obj.add_node_config_list(
            max_node_service_config.config_file_list, max_node_service_config.service_name, config_path_list)
        if ret is False:
            return False
        # patch tars
        (ret, server_id) = tars_service_obj.get_server_id(
            max_node_service_config.service_name, deploy_ip)
        if ret is False:
            return False
        return tars_service_obj.patch_tars(server_id, patch_id)

    def __upload_package(self, tars_service, max_node_service_config):
        """
        upload package
        """
        (ret, package_path) = utilities.try_to_rename_tgz_package("generated",
                                                                  self.config.tars_config.tars_pkg_dir, max_node_service_config.service_name, max_node_service_config.base_service_name)
        if ret is False:
            utilities.log_error(
                "upload package for service %s failed" % max_node_service_config.service_name)
            return (False, -1)
        return tars_service.upload_tars_package(max_node_service_config.service_name, package_path)
