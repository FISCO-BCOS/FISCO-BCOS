#!/bin/bash
SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
fisco_bcos=${SHELL_FOLDER}/fisco-bcos
cd ${SHELL_FOLDER}
nohup setsid $fisco_bcos --config config.conf --genesis genesis.json&
echo "waiting..."
sleep 5
tail -f ${SHELL_FOLDER}/log/info* |grep ++++
