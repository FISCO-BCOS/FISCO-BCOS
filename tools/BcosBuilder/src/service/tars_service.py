#!/usr/bin/python
# -*- coding: UTF-8 -*-
from common import utilities
from common.utilities import ServiceInfo
from requests_toolbelt import MultipartEncoder
import requests
import uuid
import time
import os


class TarsService:
    'basic class to access the tars'

    def __init__(self, tars_url, tars_token, app_name, deploy_ip):
        self.tars_url = tars_url
        self.tars_token = tars_token
        self.deploy_ip = deploy_ip
        if self.tars_url.endswith('/') is False:
            self.tars_url = self.tars_url + '/'
        self.add_application_url = self.tars_url + 'api/add_application'
        self.deploy_service_url = self.tars_url + 'api/deploy_server'
        self.get_port_url = self.tars_url + 'api/auto_port'
        self.add_config_url = self.tars_url + 'api/add_config_file'
        self.update_config_url = self.tars_url + 'api/update_config_file'
        # upload and publish the package
        self.upload_package_url = self.tars_url + 'api/upload_patch_package'
        self.add_task_url = self.tars_url + 'api/add_task'
        self.get_server_list_url = self.tars_url + 'api/server_list'
        self.config_file_list_url = self.tars_url + 'api/config_file_list'
        self.node_config_file_list_url = self.tars_url + 'api/node_config_file_list'
        self.get_server_patch_url = self.tars_url + "api/get_server_patch"
        self.expand_server_preview_url = self.tars_url + "api/expand_server_preview"
        self.expand_server_url = self.tars_url + "api/expand_server"
        self.fetch_config_url = self.tars_url + "api/config_file"
        self.app_name = app_name
        self.token_param = {'ticket': self.tars_token}

    def create_application(self):
        "create application"
        if self.app_exists() is True:
            # utilities.log_error(
            #    "application %s already exists" % self.app_name)
            return False
        utilities.log_debug("create application: %s" % self.app_name)
        request_data = {'f_name': self.app_name}
        response = requests.post(
            self.add_application_url, params=self.token_param, data=request_data)
        return TarsService.parse_response("create application " + self.app_name, response)

    def parse_response(operation, response):
        if response.status_code != 200:
            utilities.log_error("%s failed, error message: %s, error code: %d" %
                                (operation, response.content, response.status_code))
            return False
        result = response.json()
        error_msg = result['err_msg']
        if len(error_msg) > 0:
            utilities.log_error("%s failed, error message: %s" %
                                (operation, error_msg))
            return False
        return True

    def get_auto_port(self):
        return self.get_node_auto_port(self.deploy_ip)

    def get_node_auto_port(self, node_name):
        # get the auto_port
        utilities.log_debug("get the un-occuppied port")
        params = {"node_name": node_name, "ticket": self.tars_token}
        response = requests.get(self.get_port_url, params=params)
        if TarsService.parse_response("get the un-occupied port", response) is False:
            return (False, 0)
        result = response.json()
        if 'data' not in result:
            utilities.log_error("get empty un-occupied port")
            return (False, 0)
        node_info = result['data']
        if len(node_info) <= 0:
            utilities.log_error("get empty un-occupied port")
            return (False, 0)
        if 'port' not in node_info[0]:
            utilities.log_error("get empty un-occupied port")
            return (False, 0)
        port = node_info[0]['port']
        utilities.log_debug(
            "get the un-occupied port success, port: %s" % (port))
        return (True, int(port))

    def deploy_single_service(self, service_name, obj_name_list, allow_duplicated):
        "deploy single service"
        if self.server_exists(service_name) is True and allow_duplicated is False:
            utilities.log_error("service %s already exists." % service_name)
            return False
        utilities.log_info("deploy service %s" % service_name)
        adapters = []
        for obj_name in obj_name_list:
            # get the un-occupied port
            (ret, port) = self.get_auto_port()
            if ret is False:
                utilities.log_error(
                    "deploy service %s failed for get un-occupied port failed" % service_name)
                return False
            adapters.append({"obj_name": obj_name, "port": port, "bind_ip": self.deploy_ip, "port_type": "tcp",
                             "thread_num": 5, "max_connections": 100000, "queuecap": 50000, "queuetimeout": 20000})
        request_data = {"application": self.app_name, "server_name": service_name, "node_name": self.deploy_ip,
                        "server_type": "tars_cpp", "template_name": "tars.cpp.default", 'adapters': adapters}
        response = requests.post(
            self.deploy_service_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("deploy service " + service_name, response) is False:
            return False
        return True

    def fetch_server_config_file(self, config_file_name, server_name):
        (ret, config_id) = self.get_server_config_file_id(
            config_file_name, server_name)
        if ret is False:
            utilities.log_error("fetch server config file failed, please check the existence of specified service, service: %s, config: %s" %
                                (server_name, config_file_name))
            return (False, "")
        param = {"ticket": self.tars_token, "id": config_id}
        response = requests.get(self.fetch_config_url, params=param)
        if TarsService.parse_response("fetch service config " + server_name, response) is False:
            return (False, "")
        utilities.log_debug(
            "fetch service config file success, response: %s" % response.content)
        result = response.json()
        if "data" not in result or "config" not in result["data"]:
            utilities.log_error(
                "fetch service config file failed, response %s" % response.content)
            return (False, "")
        return (True, result["data"]["config"])

    def deploy_service_list(self, service_list, obj_list, allow_duplicated):
        "deploy service list"
        i = 0
        for service in service_list:
            if self.deploy_single_service(service, obj_list[i], allow_duplicated) is False:
                utilities.log_error("deploy service list failed, service list: %s" %
                                    service_list)
                return False
            i = i + 1
        return True

    def get_level(server_name):
        # service level
        level = 5
        # app level
        if len(server_name) == 0:
            level = 1
        return level

    def add_server_config_file(self, deploy_ip, config_file_name, server_name, config_file_path, empty_server_config):
        content = "\n"
        if os.path.exists(config_file_path) is False:
            utilities.log_error("add service config error:\n the config file %s doesn't exist, service: %s" % (
                config_file_path, server_name))
            return False
        if empty_server_config is False:
            try:
                fp = open(config_file_path)
                content = fp.read()
            except OSError as reason:
                utilities.log_error(
                    "load the configuration failed, error: %s" % str(reason))
                return False
        request_data = {"level": TarsService.get_level(server_name), "application": self.app_name, "node_name": deploy_ip,
                        "server_name": server_name, "filename": config_file_name, "config": content}
        response = requests.post(
            self.add_config_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("add application config file", response) is False:
            return False
        if response.status_code != 200:
            return False
        return True

    def add_non_empty_server_config_file(self, deploy_ip, config_file_name, server_name, config_file_path):
        "add server the config file"
        utilities.log_debug("add config file for application %s, config file path: %s, service_name: %s" % (
            self.app_name, config_file_path, server_name))
        ret = self.add_server_config_file(
            deploy_ip, config_file_name, server_name, config_file_path, False)
        if ret is False:
            ret = self.update_service_config(
                config_file_name, server_name, "", config_file_path)
        return ret

    def add_node_config_list(self, deploy_ip, config_list, service_name, config_file_list):
        i = 0
        for config_file_path in config_file_list:
            config = config_list[i]
            if self.add_non_empty_server_config_file(deploy_ip, config, service_name, config_file_path) is False:
                utilities.log_error("add_node_config_list failed, config files info: %s" %
                                    config_list)
                return False
            i = i+1
        return True

    def add_config_file(self, config_file_name, server_name, node_name, config_file_path, empty_server_config):
        "add the config file"
        (ret, id) = self.get_server_config_file_id(
            config_file_name, server_name)
        if ret is False:
            utilities.log_debug("add config file for application %s, config file path: %s, service_name: %s" %
                                (self.app_name, config_file_path, server_name))
            self.add_server_config_file(
                node_name, config_file_name, server_name, config_file_path, empty_server_config)
        return self.update_service_config(config_file_name, server_name, node_name, config_file_path)

    def update_service_config(self, config_file_name, server_name, node_name, config_file_path):
        utilities.log_debug("update config file for application %s, config file path: %s, node: %s" %
                            (self.app_name, config_file_path, node_name))
        if os.path.exists(config_file_path) is False:
            utilities.log_error("update service config error:\n the config file %s doesn't exist, service: %s" % (
                config_file_path, server_name))
            return False
        ret = True
        config_id = 0
        if len(node_name) == 0:
            (ret, config_id) = self.get_server_config_file_id(
                config_file_name, server_name)
        else:
            ret, config_id = self.get_config_file_id(
                config_file_name, server_name, node_name)
        if ret is False:
            return False
        try:
            fp = open(config_file_path)
            content = fp.read()
        except OSError as reason:
            utilities.log_error(
                "load the configuration failed, error: %s" % str(reason))
        request_data = {"id": config_id, "config": content,
                        "reason": "update config file"}
        response = requests.post(
            self.update_config_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("update config file for application " + self.app_name + ", config file:" + config_file_name, response) is False:
            return False
        return True

    def get_config_file_id(self, config_file_name, server_name, node_name):
        (ret, server_config_id) = self.get_server_config_file_id(
            config_file_name, server_name)
        if ret is False:
            return (False, 0)
        params = {"ticket": self.tars_token, "config_id": server_config_id, "level": TarsService.get_level(server_name), "application": self.app_name,
                  "server_name": server_name, "set_name": "", "set_area": "", "set_group": "", "node_name": node_name}
        response = requests.get(self.node_config_file_list_url, params=params)
        if TarsService.parse_response("query the node config file id for " + config_file_name, response) is False:
            return (False, 0)
        result = response.json()
        if "data" not in result or len(result["data"]) == 0:
            utilities.log_debug("the config %s doesn't exist" %
                                (config_file_name))
            return (False, 0)
        # try to find the config file info
        for item in result["data"]:
            if "filename" in item and item["filename"] == config_file_name and item["node_name"] == node_name:
                return (True, item["id"])
        utilities.log_error("the node config file %s not found, node: %s, :%s:" % (
            config_file_name, node_name, str(result["data"])))
        return (False, 0)

    def get_server_config_file_id(self, config_file_name, server_name):
        utilities.log_debug("query the config file id for %s" %
                            config_file_name)
        params = {"ticket": self.tars_token, "level": TarsService.get_level(server_name), "application": self.app_name,
                  "server_name": server_name, "set_name": "", "set_area": "", "set_group": ""}
        response = requests.get(
            self.config_file_list_url, params=params)
        if TarsService.parse_response("query the config file id for " + config_file_name, response) is False:
            return (False, 0)
        result = response.json()
        if "data" not in result or len(result["data"]) == 0:
            utilities.log_debug(
                "the config file id not found for %s because of empty return data, response: %s" % (config_file_name, response.content))
            return (False, 0)
        # try to find the config file info
        for item in result["data"]:
            if "filename" in item and item["filename"] == config_file_name:
                utilities.log_debug("get_server_config_file_id, server: %s, config_id: %s" % (
                    server_name, item["id"]))
                return (True, item["id"])
        utilities.log_debug("the config file %s not found" % config_file_name)
        return (False, 0)

    def add_config_list(self, config_list, service_name, node_name, config_file_list, empty_server_config):
        i = 0
        for config_file_path in config_file_list:
            config = config_list[i]
            utilities.log_info(
                "* add config for service %s, node: %s, config: %s" % (service_name, node_name, config))
            utilities.log_debug("add config for service %s, node: %s, config: %s, path: %s" % (
                service_name, node_name, config, config_file_path))
            if self.add_config_file(config, service_name, node_name, config_file_path, empty_server_config) is False:
                utilities.log_error("add_config_list failed, config files info: %s" %
                                    config_list)
                return False
            i = i+1
        return True

    def get_server_patch(self, task_id):
        utilities.log_debug("get server patch, task_id: %s" % task_id)
        params = {"task_id": task_id, "ticket": self.tars_token}
        response = requests.get(self.get_server_patch_url, params=params)
        if TarsService.parse_response("get server patch", response) is False:
            return (False, "")
        if response.status_code != 200:
            return (False, "")
        # get the id
        result = response.json()
        result_data = result['data']
        if 'id' not in result_data:
            utilities.log_error(
                "get_server_patch failed for empty return message")
            return (False, "")
        id = result_data['id']
        return (True, id)

    def upload_tars_package(self, service_name, package_path):
        """
        upload the tars package
        """
        package_name = service_name + ServiceInfo.tars_pkg_postfix
        utilities.log_debug("upload tars package for service %s, package_path: %s, package_name: %s" %
                            (service_name, package_path, package_name))
        if os.path.exists(package_path) is False:
            utilities.log_error("upload tars package for service %s failed for the path %s not exists" % (
                service_name, package_path))
            return (False, 0)
        task_id = str(uuid.uuid4())
        form_data = MultipartEncoder(fields={"application": self.app_name, "task_id": task_id, "module_name": service_name, "comment": "upload package", "suse": (
            package_name, open(package_path, 'rb'), 'text/plain/binary')})

        response = requests.post(self.upload_package_url, data=form_data, params=self.token_param, headers={
                                 'Content-Type': form_data.content_type})
        if TarsService.parse_response("upload tars package " + package_path, response) is False:
            return (False, 0)
        # get the id
        (ret, id) = self.get_server_patch(task_id)
        if ret is True:
            utilities.log_info(
                "upload tar package %s success, config id: %s" % (package_path, id))
        return (ret, id)

    def get_server_info(self, tree_node_id):
        params = {'tree_node_id': tree_node_id, "ticket": self.tars_token}
        response = requests.get(self.get_server_list_url, params=params)
        if TarsService.parse_response("get server info by tree node id: " + tree_node_id, response) is False:
            utilities.log_error("get server info by tree node id for error response, tree_node_id: %s, msg: %s" % (
                tree_node_id, response.content))
            return (False, response)
        return (True, response)

    def app_exists(self):
        (ret, response) = self.get_server_info("1" + self.app_name)
        if ret is False:
            return False
        result = response.json()
        if 'data' in result and len(result["data"]) > 0:
            return True
        return False

    def server_exists(self, service_name):
        (ret, server_id) = self.get_server_id(service_name, self.deploy_ip)
        return ret

    def get_server_id(self, service_name, node_name):
        # tree_node_id
        tree_node_id = "1" + self.app_name + ".5" + service_name
        (ret, response) = self.get_server_info(tree_node_id)
        if ret is False:
            return (False, 0)
        if TarsService.parse_response("get server list ", response) is False:
            utilities.log_error("get server info failed for error response, server name: %s, msg: %s" % (
                service_name, response.content))
            return (False, 0)
        result = response.json()
        if 'data' not in result:
            return (False, 0)
        server_infos = result['data']
        for item in server_infos:
            if "node_name" in item and len(node_name) > 0 and item["node_name"] == node_name:
                return (True, item["id"])
            if "id" in item and len(node_name) == 0:
                return (True, item["id"])
        return (False, 0)

        server_id = result["data"][0]["id"]
        return (True, server_id)

    def upload_and_publish_package(self, service_name, package_path):
        """
        upload and publish the tars package
        """
        # get the service info
        (ret, server_id) = self.get_server_id(service_name, self.deploy_ip)
        if ret is False:
            utilities.log_error(
                "upload and publish package failed for get the server info failed, server: %s" % service_name)
            return False
        # upload the tars package
        (ret, patch_id) = self.upload_tars_package(service_name, package_path)
        if ret is False:
            return False
        # patch tars
        self.patch_tars(server_id, patch_id)
        return True

    def expand_server_preview(self, server_name, node_name, expanded_node_list):
        """
        expand the server preview
        """
        utilities.log_info("expand_server_preview, app: %s, server_name: %s, expanded_node_list: %s" % (
            self.app_name, server_name, '.'.join(expanded_node_list)))
        request_data = {"application": self.app_name, "server_name": server_name, "node_name": node_name, "set": "", "expand_nodes": expanded_node_list,
                        "enable_set": "false", "set_name": "", "set_area": "", "set_group": "", "copy_node_config": "false", "nodeName": []}
        response = requests.post(
            self.expand_server_preview_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("expand server preview", response) is False:
            utilities.log_error("expand server preview for error response, server name: %s, msg: %s" % (
                server_name, response.content))
            return False
        utilities.log_info("expand server preview response %s" %
                           response.content)
        return True

    def expand_server(self, server_name, node_name, expanded_node_list, obj_list):
        """
        expand the server
        """
        utilities.log_info("expand_server, app: %s, server_name: %s" % (
            self.app_name, server_name))
        expand_servers_info = []
        for node in expanded_node_list:
            for obj in obj_list:
                (ret, port) = self.get_node_auto_port(node)
                if ret is False:
                    utilities.log_error(
                        "expand server failed for get node auto port failed, server: %s, node: %s" % (server_name, node))
                    return False
                node_info = {"bind_ip": node, "node_name": node,
                             "obj_name": obj, "port": port, "set": ""}
                expand_servers_info.append(node_info)
        request_data = {"application": self.app_name, "server_name": server_name, "set": "", "node_name": node_name,
                        "copy_node_config": "false", "expand_preview_servers": expand_servers_info}
        response = requests.post(
            self.expand_server_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("expand server", response) is False:
            utilities.log_error("expand server for error response, server name: %s, msg: %s" % (
                server_name, response.content))
            return False
        utilities.log_info("expand server response %s" % response.content)
        return True

    def expand_server_with_preview(self, server_name, node_name, expanded_node_list, obj_list):
        ret = self.expand_server_preview(
            server_name, node_name, expanded_node_list)
        if ret is False:
            utilities.log_error("expand server failed for expand preview failed, app: %s, server: %s, expanded_node_list: %s" % (
                self.app_name, server_name, '.'.join(expanded_node_list)))
            return False
        return self.expand_server(server_name, node_name, expanded_node_list, obj_list)

    def patch_tars(self, server_id, patch_id):
        utilities.log_debug("patch tars for application %s, server_id: %s, patch_id: %s" % (
            self.app_name, server_id, patch_id))
        items = [{"server_id": server_id, "command": "patch_tars", "parameters": {
            "patch_id": patch_id, "bak_flag": 'false', "update_text": "", "group_name": ""}}]
        request_data = {"serial": 'true', "items": items}
        response = requests.post(
            self.add_task_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("patch tars ", response) is False:
            utilities.log_error("patch tars failed for error response, server id: %s, msg: %s" % (
                server_id, response.content))
            return False
        utilities.log_debug("patch tars response %s" % response.content)
        return True

    def add_task(self, service_name, command):
        """
        current supported commands are: stop, restart, undeploy_tars, patch_tars
        """
        utilities.log_debug("add_task for service %s, command is %s" %
                            (service_name, command))
        (ret, server_id) = self.get_server_id(service_name, self.deploy_ip)
        if ret is False:
            utilities.log_error("%s failed for get server id failed, please check the existence of %s" % (
                command, service_name))
            return False
        items = [{"server_id": server_id, "command": command, "parameters": {}}]
        request_data = {"serial": 'true', "items": items}
        response = requests.post(
            self.add_task_url, params=self.token_param, json=request_data)
        if TarsService.parse_response("execute command " + command, response) is False:
            utilities.log_error("add_task failed for error response, server name: %s, msg: %s" % (
                service_name, response.content))
            return False
        return True

    def stop_server(self, service_name):
        """
        stop the givn service
        """
        return self.add_task(service_name, "stop")

    def stop_server_list(self, server_list):
        for server in server_list:
            if self.stop_server(server) is False:
                return False
        return True

    def restart_server(self, service_name):
        """
        restart the given service
        """
        return self.add_task(service_name, "restart")

    def undeploy_tars(self, service_name):
        """
        undeploy the tars service
        """
        return self.add_task(service_name, "undeploy_tars")

    def undeploy_server_list(self, server_list):
        for server in server_list:
            if self.undeploy_tars(server) is False:
                return False
        return True

    def restart_server_list(self, server_list):
        for server in server_list:
            if self.restart_server(server) is False:
                return False
            time.sleep(5)
        return True

    def get_service_list(self):
        return self.get_server_info("1" + self.app_name)

    def upload_and_publish_package_list(self, service_list, service_path_list):
        i = 0
        for service in service_list:
            service_path = service_path_list[i]
            self.upload_and_publish_package(service, service_path)
            i = i+1
            time.sleep(10)
