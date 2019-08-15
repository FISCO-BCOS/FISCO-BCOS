#/usr/bin/python
# -*- coding: UTF-8 -*-
import subprocess
import sys
import os

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
    output = subprocess.PIPE
    flag = 0
    # for background execution
    if("nohup" in command) or ("\&" in command):
        output = open('result.log', 'w')
        flag = 1
    child = subprocess.Popen(command.split(), shell=False, stdout=output, stderr=output, preexec_fn=os.setpgrp)
    ret = child.communicate()
    err = ""
    if (flag == 0):
        result = ret[0]
        err = ret[1]
    else:
        status, result = execute_command("cat result.log")
        
    status = child.returncode
    if(status != 0):
        LOG_ERROR("=== execute " + command + " failed, information = " + result + " error information:" + err.decode('utf-8'))
    if err is not None:
        result += err.decode('utf-8')
    return (status, result)
    