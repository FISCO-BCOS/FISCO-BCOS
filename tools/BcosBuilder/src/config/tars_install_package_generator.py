#!/usr/bin/python
# -*- coding: UTF-8 -*-

import configparser
import os
import platform
import shutil

from common import utilities
from common.utilities import ConfigInfo
from common.utilities import execute_command_and_getoutput


def get_tars_proxy_config_section_index(ini_config, section):
    if not ini_config.has_section(section):
        ini_config.add_section(section)
        return 0

    index = 0
    while True:
        proxy_index_str = "proxy." + str(index)
        if proxy_index_str in ini_config[section]:
            index += 1
            continue
        return index


def generate_tars_proxy_config(output_dir, agency_name, chain_id,
                               tars_service_port):
    chain_tars_proxy_conf_dir = os.path.join(output_dir, chain_id)

    if os.path.exists(chain_tars_proxy_conf_dir) is False:
        utilities.mkdir(chain_tars_proxy_conf_dir)

    agency_tars_conf_path = os.path.join(chain_tars_proxy_conf_dir,
                                         agency_name + "_tars_proxy.ini")

    tars_proxy_config = configparser.ConfigParser(comment_prefixes='/',
                                                  allow_no_value=True)

    if os.path.exists(agency_tars_conf_path):
        tars_proxy_config.read(agency_tars_conf_path)

    for key, value in tars_service_port.items():
        index = get_tars_proxy_config_section_index(tars_proxy_config, key)
        # utilities.log_info("* generate tars proxy config, key: %s, index: %s" % (key, str(index)))
        tars_proxy_config[key]["proxy." + str(index)] = value

    with open(agency_tars_conf_path, 'w') as f:
        tars_proxy_config.write(f)

    return


def initialize_tars_config_env_variables(config_items, tars_conf_file):
    sys_name = platform.system()
    if sys_name.lower() == "darwin":
        sed = "sed -i .bak "
    else:
        sed = "sed -i "

    for key, value in config_items.items():
        sed_command = sed + "s/" + key + "/" + value + "/g " + tars_conf_file
        execute_command_and_getoutput(sed_command)

    # remove .bak files if exist
    if os.path.exists(tars_conf_file + ".bak"):
        os.remove(tars_conf_file + ".bak")


def generate_tars_package(pkg_dir, exec_name, service_name, agency_name, chain_id,
                          service_type, tars_pkg_dir):
    conf_dir = os.path.join(pkg_dir, 'conf')

    utilities.log_info("* generate tars install package for %s:%s:%s:%s:%s:%s" % (
       exec_name, service_name, agency_name, chain_id, service_type, tars_pkg_dir))

    if not os.path.exists(conf_dir):
        utilities.mkdir(conf_dir)

    # copy start.sh stop.sh tars.conf
    tars_start_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_start.sh")
    tars_stop_file = os.path.join(ConfigInfo.tpl_abs_path, "tars_stop.sh")
    tars_conf_file = os.path.join(ConfigInfo.tpl_abs_path,
                                  "tars_" + service_type + ".conf")

    tars_start_all_file = os.path.join(ConfigInfo.tpl_abs_path,
                                       "tars_start_all.sh")
    tars_stop__all_file = os.path.join(ConfigInfo.tpl_abs_path,
                                       "tars_stop_all.sh")

    start_all_file = os.path.join(pkg_dir, "../", "start_all.sh")
    stop_all_file = os.path.join(pkg_dir, "../", "stop_all.sh")

    start_file = os.path.join(pkg_dir, "start.sh")
    stop_file = os.path.join(pkg_dir, "stop.sh")
    conf_file = os.path.join(conf_dir, "tars.conf")

    shutil.copy(tars_start_file, start_file)
    shutil.copy(tars_stop_file, stop_file)
    shutil.copy(tars_conf_file, conf_file)

    if not os.path.exists(start_all_file):
        shutil.copy(tars_start_all_file, start_all_file)

    if not os.path.exists(stop_all_file):
        shutil.copy(tars_stop__all_file, stop_all_file)

    # copy service binary exec
    shutil.copy(os.path.join(tars_pkg_dir, exec_name, exec_name),
                pkg_dir)

    sys_name = platform.system()
    if sys_name.lower() == "darwin":
        sed = "sed -i .bak "
    else:
        sed = "sed -i "

    sed_cmd = sed + "s/@SERVICE_NAME@/" + exec_name + "/g " + start_file
    execute_command_and_getoutput(sed_cmd)

    sed_cmd = sed + "s/@SERVICE_NAME@/" + exec_name + "/g " + stop_file
    execute_command_and_getoutput(sed_cmd)

    sed_cmd = sed + "s/@TARS_APP@/" + chain_id + "/g " + conf_file
    execute_command_and_getoutput(sed_cmd)
    sed_cmd = sed + "s/@TARS_SERVER@/" + service_name + "/g " + conf_file
    execute_command_and_getoutput(sed_cmd)

    # remove .bak files if exist
    if os.path.exists(start_file + ".bak"):
        os.remove(start_file + ".bak")

    if os.path.exists(stop_file + ".bak"):
        os.remove(stop_file + ".bak")

    if os.path.exists(conf_file + ".bak"):
        os.remove(conf_file + ".bak")
