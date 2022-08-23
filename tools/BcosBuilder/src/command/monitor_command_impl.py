#!/usr/bin/python
# -*- coding: UTF-8 -*-
from controller.monitor_controller import MonitorController
from common import utilities


class MonitorCommandImpl:
    def __init__(self, config, node_type, output_dir):
        self.Monitor_controller = MonitorController(config, node_type, output_dir)

    def deploy_monitor(self):
        function = "generate_and_deploy_monitor_services"
        notice_info = "deploy all nodes monitor"
        return self.execute_command(function, notice_info)

    def start_monitor(self):
        function = "start_monitor_services"
        notice_info = "start all nodes monitor"
        return self.execute_command(function, notice_info)

    def stop_monitor(self):
        function = "stop_monitor_services"
        notice_info = "stop all nodes monitor"
        return self.execute_command(function, notice_info)

    def execute_command(self, function, notice_info):
        utilities.print_split_info()
        utilities.print_badge(notice_info)
        ret = getattr(self.Monitor_controller, function)()
        if ret is True:
            utilities.print_badge("%s success" % notice_info)
        else:
            utilities.log_error("%s failed" % notice_info)
        utilities.print_split_info()
        return ret
