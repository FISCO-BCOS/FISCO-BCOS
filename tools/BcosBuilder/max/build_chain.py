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
        parser_handler.chain_operations(args, "max")
        parser_handler.download_binary_operation(args, "max")
    except Exception as error:
        utilities.log_error("%s" % error)


if __name__ == "__main__":
    main()
