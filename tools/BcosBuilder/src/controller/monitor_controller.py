#!/usr/bin/python
# -*- coding: UTF-8 -*-
from config.monitor_config_generator import MonitorConfigGenerator


class MonitorController:
    """
    the monitor controller
    """

    def __init__(self, config, node_type, output_dir):
        self.config = config
        self.binary_name = ""
        self.download_url = ""
        self.node_type = node_type
        self.monitor_generator = MonitorConfigGenerator(
            self.config, node_type, output_dir)

    def generate_monitor_config(self):
        if self.monitor_generator.generate_monitor_config() is False:
            return False
        return self.monitor_generator.generate_mtail_config()

    def generate_and_deploy_monitor_services(self):
        if self.generate_monitor_config() is False:
            return False
        return True

    def start_monitor_services(self):
        if self.monitor_generator.start_monitor_config() is False:
            return False
        return True

    def stop_monitor_services(self):
        if self.monitor_generator.stop_monitor_config() is False:
            return False
        return True
