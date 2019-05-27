#/usr/bin/python
# -*- coding: UTF-8 -*-

import commands
import sys

def LOG_ERROR(msg):
    """
    print error msg with red color
    """
    print('\033[31m' + msg + '\033[0m')
    sys.exit(-1)

def LOG_INFO(msg):
    """
    print information with green color
    """
    print('\033[32m' + msg + '\033[0m')

def execute_command(command):
    """
    execute command
    """
    (status, result) = commands.getstatusoutput(command)
    if(status != 0):
        LOG_ERROR("=== execute " + command + " failed, information = " + result)
    return (status, result)
    