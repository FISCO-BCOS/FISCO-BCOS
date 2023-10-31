#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
import os
import shutil

from config.tars_install_package_generator import get_tars_proxy_config_section_index


class TarsConfigGenerator:

    def __init__(self, tars_conf):
        self.tars_conf = tars_conf
        self.tars_proxy_ini = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        if os.path.exists(tars_conf):
            self.tars_proxy_ini.read(tars_conf)

    def append_config_item(self, service_name, endpoint):
        index = get_tars_proxy_config_section_index(self.tars_proxy_ini, service_name)
        self.tars_proxy_ini[service_name]["proxy." + str(index)] = endpoint

    def get_config_items(self, service_name):
        if service_name in self.tars_proxy_ini:
            return self.tars_proxy_ini[service_name]
        return None

    def restore_init_config(self, tars_conf_path):
        with open(tars_conf_path, 'w') as f:
            self.tars_proxy_ini.write(f)
