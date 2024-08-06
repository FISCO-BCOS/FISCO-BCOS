#!/bin/bash
current_path=`pwd`
fisco_bcos_rpc_path="FISCOBCOS-RPC-API"
fisco_bcos_path="../build/fisco-bcos-air/fisco-bcos"
build_chain_path="BcosAirBuilder/build_chain.sh"
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

wait_and_start()
{
    LOG_INFO "Try to start all"
    if [ -z "$(bash start_all.sh | grep 'Exceed waiting time')" ]; then
        LOG_INFO "Start all success"
        LOG_INFO "Waiting connection established"
        sleep 20
    else
        bash stop_all.sh
        LOG_WARN "Another testing is running. Waiting 20s and re-try to start all."
        sleep 20
        wait_and_start
    fi
}

init()
{
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -p 30300,20200 -l "127.0.0.1:4" -e ${fisco_bcos_path}
    open_disablessl
    cd ..
    wait_and_start
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

clear_node()
{
    cd ${current_path}
    if [ -d "nodes" ]; then
        bash nodes/127.0.0.1/stop_all.sh
        rm -rf nodes
    fi
}

open_disablessl(){
  cd ${current_path}
  cd nodes/127.0.0.1/node0
  local sed_cmd="sed -i"
  if [ "$(uname)" == "Darwin" ];then
      sed_cmd="sed -i .bkp"
  fi
  ${sed_cmd}  's/;disable_ssl=true/disable_ssl=true/g' config.ini
}



init