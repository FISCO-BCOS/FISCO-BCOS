#!/usr/bin/python
# -*- coding: UTF-8 -*-
import sys
# Note: here can't be refactored by autopep
sys.path.append("src/")

from common import utilities
from common.utilities import CommandInfo
from common.utilities import ServiceInfo
from config.chain_config import ChainConfig
from command.service_command_impl import ServiceCommandImpl
from command.node_command_impl import NodeCommandImpl
import argparse
import toml
import os


def parse_command():
    parser = argparse.ArgumentParser(description='build_chain')
    parser.add_argument(
        "-o", '--op', help="[required] the command, support \'" + ', '.join(CommandInfo.command_list) + "\'", required=True)
    parser.add_argument(
        "-c", "--config", help="[optional] the config file, default is config.toml", default="config.toml")
    parser.add_argument("-t", "--type",
                        help="[required] the service type, now support \'" + ', '.join(ServiceInfo.supported_service_type) + "\'", required=True)
    args = parser.parse_args()
    return args


def main():
    args = parse_command()
    if os.path.exists(args.config) is False:
        utilities.log_error("The config file '%s' not found!" % args.config)
        sys.exit(-1)
    toml_config = toml.load(args.config)
    chain_config = ChainConfig(toml_config)
    op_type = args.type
    if op_type not in ServiceInfo.supported_service_type:
        utilities.log_error("the service type must be " +
                            ', '.join(ServiceInfo.supported_service_type))
        return
    command = args.op
    if op_type == ServiceInfo.rpc_service_type or op_type == ServiceInfo.gateway_service_type:
        if command in CommandInfo.service_command_impl.keys():
            command_impl = ServiceCommandImpl(chain_config, args.type)
            impl_str = CommandInfo.service_command_impl[command]
            cmd_func_attr = getattr(command_impl, impl_str)
            cmd_func_attr()
            return
    if op_type == ServiceInfo.node_service_type:
        if command in CommandInfo.node_command_to_impl.keys():
            command_impl = NodeCommandImpl(chain_config)
            impl_str = CommandInfo.node_command_to_impl[command]
            cmd_func_attr = getattr(command_impl, impl_str)
            cmd_func_attr()
            return
    utilities.log_info("unimplemented command")


if __name__ == "__main__":
    main()
