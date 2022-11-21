#!/usr/bin/python
# -*- coding: UTF-8 -*-
# Note: here can't be refactored by autopep
import sys
sys.path.append("../src/")

from common import utilities
from common import parser_handler

def main():
    try:
        args = parser_handler.parse_command()
        parser_handler.build_package_operation(args, "proc")
        parser_handler.chain_operations(args, "pro")
        parser_handler.create_subnet_operation(args)
        parser_handler.add_vxlan_operation(args)
        parser_handler.download_binary_operation(args, "pro")
        parser_handler.merge_config_operation(args)
    except Exception as error:
        utilities.log_error("%s" % error)

if __name__ == "__main__":
    main()
