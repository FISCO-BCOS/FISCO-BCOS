#!/usr/bin/python
# -*- coding: UTF-8 -*-
import configparser
import os
import shutil

class TarsConfigGenerator:

    def __init__(self, tars_conf):
        self.tars_conf = tars_conf
        self.tars_ini = configparser.ConfigParser(
            comment_prefixes='/', allow_no_value=True)
        if os.path.exists(tars_conf):
            self.tars_ini.read(tars_conf)

    def __get_tars_proxy_conf_section_index(self, section):
        if not self.tars_ini.has_section(section):
            self.tars_ini.add_section(section)
            return 0
        
        index = 0
        while True:
            proxy_index_str = "proxy." + str(index)
            if proxy_index_str in self.tars_ini[section]:
                index += 1
                continue
            return index
    
    def append_config_item(self, service_name, endpoint):
        index = self.__get_tars_proxy_conf_section_index(service_name)
        self.tars_ini[service_name]["proxy." + str(index)] = endpoint

    def get_service_config_items(self, service_name):
        if service_name in self.tars_ini:
            return self.tars_ini[service_name]
        return None

    def restore_init_config(self, tars_conf_path):
        with open(tars_conf_path, 'w') as f:
            self.tars_ini.write(f)