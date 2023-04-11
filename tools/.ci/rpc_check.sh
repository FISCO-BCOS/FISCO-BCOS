#!/bin/bash
current_path=`pwd`
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

open_disablessl(){
  cd ${current_path}
  cd nodes/127.0.0.1/node0
  local sed_cmd="sed -i"
  if [ "$(uname)" == "Darwin" ];then
      sed_cmd="sed -i .bkp"
  fi
  ${sed_cmd}  's/;disable_ssl=true/disable_ssl=true/g' config.ini
}

rpc_apiTest()
{
    cd ${current_path}
    LOG_INFO ">>>>>>>>>>> Run API tests  <<<<<<<<<<<<<<"
    newman run fiscobcos.rpcapi.collection.json --reporters junit --reporter-junit-export rpcapitest-report.xml
}

open_disablessl
start_node
rpc_apiTest
stop_node
