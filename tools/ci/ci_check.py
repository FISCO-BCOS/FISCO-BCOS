#!/usr/bin/python
# -*- coding: UTF-8 -*-
import sys
import time
import basic_check
import getopt

class Usage(Exception):
    def __init__(self, msg):
        self.msg = msg

def check_basic_status(basic_check_object):
    """
    check all nodes status
    """
    for i in range(basic_check_object.node_num):
        basic_check_object.check_sendTransaction(i, 10)
        basic_check_object.check_all(i)

def check_status_except(basic_check_object, node_id):
    """
    check status for all node except the specified node
    """
    for i in range(basic_check_object.node_num):
        if(i == node_id):
            continue
        basic_check_object.check_all(i)

def check_sync(basic_check_object, wait_time):
    """
    check sync and 3*f+1
    """
    for i in range(basic_check_object.node_num):
        basic_check_object.stop_and_delete_data(i)
        check_status_except(basic_check_object, i)
        time.sleep(wait_time)
        basic_check_object.start_a_node(i)
        time.sleep(10)
        basic_check_object.check_all(i)


def main(argv=None):
    """
    check basic
    """
    basic_check_object = basic_check.BasicCheck()
    #basic_check_object.build_localdb_blockchain()    
    check_basic_status(basic_check_object)
    check_sync(basic_check_object, 1)
    #basic_check_object.stop_all_nodes()

if __name__ == "__main__":
    main(sys.argv)
