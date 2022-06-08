#!/usr/bin/python
# -*- coding: UTF-8 -*-
import sys
import os
import subprocess
import logging
import re

logging.basicConfig(format='%(message)s',
                    level=logging.INFO)


class ServiceInfo:
    node_service_type = "node"
    rpc_service_type = "rpc"
    gateway_service_type = "gateway"
    monitor_service_type = "monitor"

    ssl_file_list = ["ca.crt", "ssl.key", "ssl.crt"]
    sm_ssl_file_list = ["sm_ca.crt", "sm_ssl.key",
                        "sm_ssl.crt", "sm_enssl.key", "sm_enssl.crt"]

    rpc_service = "BcosRpcService"
    rpc_service_obj = ["RpcServiceObj"]
    gateway_service = "BcosGatewayService"
    gateway_service_obj = ["GatewayServiceObj"]

    single_node_service = "BcosNodeService"
    single_node_obj_name_list = [
        "LedgerServiceObj", "SchedulerServiceObj", "TxPoolServiceObj", "PBFTServiceObj", "FrontServiceObj"]

    max_node_service = "BcosMaxNodeService"
    max_node_service_obj = single_node_obj_name_list
    executor_service = "BcosExecutorService"
    executor_service_obj = ["ExecutorServiceObj"]

    supported_vm_types = ["evm", "wasm"]
    supported_consensus_list = ["pbft"]
    tars_pkg_postfix = ".tgz"
    default_listen_ip = "0.0.0.0"
    cert_generationscript_path = "../src/scripts/generate_cert.sh"
    supported_service_type = [node_service_type, monitor_service_type,
                              rpc_service_type, gateway_service_type]


class ConfigInfo:
    tpl_abs_path = "../src/tpl/"
    tpl_src_mtail_path = "../src/scripts/"
    tpl_binary_path = "./binary/"
    tpl_monitor_path = "../docker/host/linux/monitor/"
    pwd_path = os.getcwd()
    rpc_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "config.ini.rpc")
    gateway_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "config.ini.gateway")
    genesis_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "config.genesis")
    node_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "config.ini.node")
    mtail_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "node.mtail")
    monitor_config_tpl_path = os.path.join(
        pwd_path, tpl_monitor_path, "")
    prometheus_config_tpl_path = os.path.join(
        pwd_path, tpl_monitor_path, "prometheus/prometheus.yml")
    executor_config_tpl_path = os.path.join(
        pwd_path, tpl_abs_path, "config.ini.executor")


class CommandInfo:
    gen_config = "gen-config"
    upload = "upload"
    deploy = "deploy"
    upgrade = "upgrade"
    undeploy = "undeploy"
    start = "start"
    stop = "stop"
    expand = "expand"
    network_create_subnet = "create-subnet"
    network_add_vxlan = "add-vxlan"
    download_binary = "download_binary"
    download_type = ["cdn", "git"]
    default_binary_version = "v3.0.0-rc4"
    command_list = [gen_config, upload, deploy,
                    upgrade, undeploy, expand, start, stop]
    service_command_list_str = ', '.join(command_list)
    chain_sub_parser_name = "chain"
    node_command_to_impl = {gen_config: "gen_node_config", upload: "upload_nodes", deploy: "deploy_nodes",
                            upgrade: "upgrade_nodes", undeploy: "undeploy_nodes", start: "start_all", stop: "stop_all", expand: "expand_nodes"}
    monitor_command_to_impl = {deploy: "deploy_monitor",
                               start: "start_monitor", stop: "stop_monitor"}
    service_command_impl = {gen_config: "gen_service_config", upload: "upload_service", deploy: "deploy_service",
                            upgrade: "upgrade_service", undeploy: "delete_service", start: "start_service", stop: "stop_service", expand: "expand_service"}


def log_error(error_msg):
    logging.error("\033[31m%s \033[0m" % error_msg)


def log_info(error_msg):
    logging.info("\033[32m%s \033[0m" % error_msg)


def format_info(info):
    return ("\033[32m%s \033[0m" % info)


def log_debug(error_msg):
    logging.debug("%s" % error_msg)


def get_item_value(config, key, default_value, must_exist, desc):
    if key in config:
        return config[key]
    if must_exist:
        raise Exception("the value for %s.%s must be set" % (desc, key))
    return default_value


def get_value(config, section, key, default_value, must_exist):
    if section in config and key in config[section]:
        return config[section][key]
    if must_exist:
        raise Exception("the value for %s must be set" % key)
    return default_value


def execute_command_and_getoutput(command):
    status, output = subprocess.getstatusoutput(command)
    if status != 0:
        log_error(
            "execute command %s failed, error message: %s" % (command, output))
        return (False, output)
    return (True, output)


def execute_command(command):
    (ret, result) = execute_command_and_getoutput(command)
    return ret


def mkdir(path):
    if not os.path.exists(path):
        os.makedirs(path)


def mkfiledir(filepath):
    parent_dir = os.path.abspath(os.path.join(filepath, ".."))
    mkdir(parent_dir)


def generate_service_name(prefix, service_name):
    return prefix + service_name


def convert_bool_to_str(value):
    if value is True:
        return "true"
    return "false"


def generate_cert_with_command(sm_type, command, outputdir, ca_cert_info):
    """
    generate cert for the network
    """
    sm_mode = ""
    if sm_type is True:
        sm_mode = " -s"
    generate_cert_cmd = "bash %s -o %s -c %s %s %s" % (
        ServiceInfo.cert_generationscript_path, outputdir, command, sm_mode, ca_cert_info)
    if execute_command(generate_cert_cmd) is False:
        log_error("%s failed" % command)
        sys.exit(-1)
    return True


def execute_monitor_with_command(monitor_execute__scripts_path):
    """
    execute common for the monitor
    """
    execute_monitor_cmd = "bash %s" % (
        monitor_execute__scripts_path)
    if execute_command(execute_monitor_cmd) is False:
        log_error("%s failed exec " % monitor_execute__scripts_path)
        sys.exit(-1)
    return True


def execute_mtail_with_command(mtail_execute_scripts_path, mtail_listen_port):
    """
    execute mtail for the monitor
    """
    execute_mtail_cmd = "bash %s %s " % (
        mtail_execute_scripts_path, mtail_listen_port)
    if execute_command(execute_mtail_cmd) is False:
        log_error("%s failed start" % mtail_execute_scripts_path)
        sys.exit(-1)
    return True


def execute_ansible_copy_with_command(deploy_ip, srcDir, destDir):
    """
    execute ansible for the monitor

    """
    execute_copy_matil_cmd = 'ansible %s -m copy -a "src=%s dest=%s/ mode=0755"' % (
        deploy_ip, srcDir, destDir)
    if execute_command(execute_copy_matil_cmd) is False:
        log_error("%s failed copy mtail config for ansible" % srcDir)
        sys.exit(-1)
    return True


def execute_ansible_with_command(start_scripts_path, deploy_ip, mtail_listen_port):
    """
    execute ansible for the monitor

    """
    execute_matil_cmd = 'ansible %s -m shell -a "bash %s %s "' % (
        deploy_ip, start_scripts_path, mtail_listen_port)
    if execute_command(execute_matil_cmd) is False:
        log_error("%s failed start mtail config by ansible" %
                  start_scripts_path)
        sys.exit(-1)
    return True


def execute_ansible_with_monitor_command(start_scripts_path, deploy_ip):
    """
    execute ansible for the monitor

    """
    execute_matil_cmd = 'ansible %s -m shell -a "%s"' % (
        deploy_ip, start_scripts_path)
    if execute_command(execute_matil_cmd) is False:
        log_error("%s failed start mtail config by ansible" %
                  start_scripts_path)
        sys.exit(-1)
    return True


def generate_private_key(sm_type, outputdir):
    return generate_cert_with_command(sm_type, "generate_private_key", outputdir, "")


def generate_cert(sm_type, outputdir):
    return generate_cert_with_command(sm_type, "generate_all_cert", outputdir, "")


def generate_ca_cert(sm_type, cacert_dir):
    command = "generate_ca_cert"
    ca_cert_info = "-d %s" % cacert_dir
    return generate_cert_with_command(sm_type, command, cacert_dir, ca_cert_info)


def generate_node_cert(sm_type, ca_cert_path, outputdir):
    command = "generate_node_cert"
    ca_cert_info = "-d %s" % ca_cert_path
    return generate_cert_with_command(sm_type, command, outputdir, ca_cert_info)


def generate_sdk_cert(sm_type, ca_cert_path, outputdir):
    command = "generate_sdk_cert"
    ca_cert_info = "-d %s" % ca_cert_path
    return generate_cert_with_command(sm_type, command, outputdir, ca_cert_info)


def print_split_info():
    log_info("=========================================================")


def print_badage(badage):
    log_info("----------- %s -----------" % badage)


def try_to_rename_tgz_package(root_path, tars_pkg_path, service_name, org_service_name):
    renamed_package_path = ""
    if os.path.exists(tars_pkg_path) is False:
        log_error("rename pkg: the tars pkg path %s doesn't exist, service: %s" % (
            tars_pkg_path, service_name))
        return (False, renamed_package_path)

    org_package_name = org_service_name + ServiceInfo.tars_pkg_postfix
    org_package_path = os.path.join(tars_pkg_path, org_package_name)
    if os.path.exists(org_package_path) is False:
        log_error("rename pkg: the tars pkg path %s doesn't exist, service: %s" % (
            tars_pkg_path, service_name))
        return (False, renamed_package_path)

    unzip_binary_path = os.path.join("./", org_service_name, org_service_name)
    if service_name == org_service_name:
        return (True, org_package_path)
    renamed_package_name = service_name + ServiceInfo.tars_pkg_postfix
    renamed_package_path = os.path.join("./", renamed_package_name)
    renamed_binary_path = os.path.join("./", service_name, service_name)

    mkdir_command = "mkdir -p %s" % os.path.join("./", service_name)
    unzip_command = "tar -xvf %s" % org_package_path
    mv_command = "mv %s %s && rm -rf %s" % (
        unzip_binary_path, renamed_binary_path, os.path.join("./", org_service_name))
    zip_command = "tar -cvzf %s %s" % (renamed_package_path,
                                       renamed_binary_path)
    rm_command = "rm -rf %s" % os.path.join("./", service_name)
    generated_package_path = os.path.join(root_path, renamed_package_path)
    mv_pkg_command = "mkdir -p %s && mv %s %s" % (
        root_path, renamed_package_path, root_path)
    command = "%s && %s && %s && %s && %s && %s" % (
        mkdir_command, unzip_command, mv_command, zip_command, rm_command, mv_pkg_command)
    ret = execute_command(command)
    if ret is False:
        log_error("try_to_rename_tgz_package failed, service: %s" %
                  service_name)
    return (ret, generated_package_path)


def check_service_name(tag, service_name):
    """
    Note: tars service name can only contain letters and numbers
    """
    service_name_len = len(service_name)
    ret = re.search(r'^[A-Za-z0-9]+', service_name).span()
    if ret is None or (ret[0] != 0 or ret[1] != service_name_len):
        raise Exception(
            "the %s must be letters|numbers, invalid value: %s" % (tag, service_name))
