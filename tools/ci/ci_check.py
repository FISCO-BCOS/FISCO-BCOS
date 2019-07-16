#!/usr/bin/python
# -*- coding: UTF-8 -*-
import sys
import time
import basic_check


class Usage(Exception):
    def __init__(self, msg):
        self.msg = msg


def check_basic_status(basic_check_object):
    """
    check all nodes status
    """
    for i in range(basic_check_object.node_num):
        basic_check_object.check_sendTransaction(i, 10)
        time.sleep(2)
        basic_check_object.check_all(i)


def check_status_except(basic_check_object, node_id):
    """
    check status for all node except the specified node
    """
    for i in range(basic_check_object.node_num):
        if(i == node_id):
            continue
        basic_check_object.check_all(i, node_id)
        break


def check_sync(basic_check_object, wait_time):
    """
    check sync and 3*f+1
    """
    for i in range(basic_check_object.node_num):
        basic_check_object.stop_and_delete_data(i)
        check_status_except(basic_check_object, i)
        time.sleep(wait_time)
        basic_check_object.start_a_node(i)
        time.sleep(5)
        basic_check_object.check_sync(i)
        time.sleep(5)
        basic_check_object.check_consensus(i)


def main(argv=None):
    """
    check basic
    """
    basic_check_object = basic_check.BasicCheck()
    check_basic_status(basic_check_object)
    time.sleep(5)
    check_sync(basic_check_object, 1)


if __name__ == "__main__":
    main(sys.argv)
