#!/bin/bash
console_branch="master"
fisco_bcos_path="../build/fisco-bcos-air/fisco-bcos"
build_chain_path="BcosAirBuilder/build_chain.sh"
current_path=`pwd`
node_list="node0 node1 node2 node3"
LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m ${content}\033[0m"
}

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[ERROR] ${content}\033[0m"
}

stop_node()
{
    cd ${current_path}
    LOG_INFO "exit_node >>>>>>> stop all nodes <<<<<<<<<<<"
    if [ -z "$(bash nodes/127.0.0.1/stop_all.sh |grep 'Exceed waiting time')" ];then
        LOG_INFO "Stop success"
    else
        LOG_ERROR "Could not stop the node"
        exit 1
    fi
}

start_node()
{
    cd ${current_path}
    LOG_INFO " >>>>>>>>>> start  all nodes <<<<<<<<<<<"
    if [ -z "$(bash nodes/127.0.0.1/start_all.sh |grep 'Exceed waiting time')" ];then
        LOG_INFO "Start success"
    else
        LOG_ERROR "Could not start the node"
        exit 1
    fi
}



# non-sm test
LOG_INFO "======== FISCOBCOS 3.0 RPC API check ========"
start_node
