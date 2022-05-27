#!/usr/bin/python
# -*- coding: UTF-8 -*-
from controller.node_controller import NodeController
from common import utilities


class NodeCommandImpl:
    def __init__(self, config, node_type):
        self.node_controller = NodeController(config, node_type)

    def gen_node_config(self):
        function = "generate_all_config"
        notice_info = "generate config for all nodes"
        return self.execute_command(function, notice_info)

    def start_all(self):
        function = "start_group"
        notice_info = "start all nodes of the given group"
        return self.execute_command(function, notice_info)

    def stop_all(self):
        function = "stop_group"
        notice_info = "stop all nodes of the given group"
        return self.execute_command(function, notice_info)

    def upgrade_nodes(self):
        function = "upgrade_group"
        notice_info = "upgrade all nodes of the given group"
        return self.execute_command(function, notice_info)

    def deploy_nodes(self):
        function = "generate_and_deploy_group_services"
        notice_info = "deploy all nodes of the given group"
        return self.execute_command(function, notice_info)

    def upload_nodes(self):
        function = "deploy_group_services"
        notice_info = "upload all nodes config of the given group"
        return self.execute_command(function, notice_info)

    def undeploy_nodes(self):
        function = "undeploy_group"
        notice_info = "undeploy all nodes of the given group"
        return self.execute_command(function, notice_info)

    def generate_expand_config(self):
        function = "generate_all_expand_config"
        notice_info = "generate expand config for the given group"
        return self.execute_command(function, notice_info)

    def expand_nodes(self):
        function = "expand_and_deploy_all_nodes"
        notice_info = "expand nodes for the given group"
        return self.execute_command(function, notice_info)

    def expand_executors(self):
        function = "expand_and_deploy_all_executors"
        notice_info = "expand executors for the given group"
        return self.execute_command(function, notice_info)

    def execute_command(self, function, notice_info):
        utilities.print_split_info()
        utilities.print_badage(notice_info)
        ret = getattr(self.node_controller, function)()
        if ret is True:
            utilities.print_badage("%s success" % notice_info)
        else:
            utilities.log_error("%s failed" % notice_info)
        utilities.print_split_info()
        return ret
