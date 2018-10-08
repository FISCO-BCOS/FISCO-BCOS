#!/bin/bash
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
fisco_bcos=${SHELL_FOLDER}/fisco-bcos
weth_pid=`ps aux|grep "${fisco_bcos}"|grep -v grep|awk '{print $2}'`
kill -9 ${weth_pid}
