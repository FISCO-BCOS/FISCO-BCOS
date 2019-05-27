#/usr/bin/python
# -*- coding: UTF-8 -*-
import subprocess
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
    child = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ret = child.communicate()
    result = ret[0]
    err = ret[1]
    status = child.returncode
    if(status != 0):
        LOG_ERROR("=== execute " + command + " failed, information = " + result + " error information:" + err.decode('utf-8'))
    if not err is None:
        result += err.decode('utf-8')
    return (status, result)
    