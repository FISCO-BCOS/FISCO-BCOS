#!/bin/bash
set -e
LOG_ERROR()
{
    content=${1}
    echo -e "\033[31m${content}\033[0m"
}

LOG_INFO()
{
    content=${1}
    echo -e "\033[32m${content}\033[0m"
}

execute_cmd()
{
    command="${1}"
    #LOG_INFO "RUN: ${command}"
    eval ${command}
    ret=$?
    if [ $ret -ne 0 ];then
        LOG_ERROR "FAILED execution of command: ${command}"
        clear_cache
        exit 1
    fi
}

group_id=
all_groups="false"
ip="127.0.0.1"
rpc_port=
rpc_command=
# check the params
checkParam()
{
    #check group_id
    if [ "${all_groups}" == "false" -a "${group_id}" == "" ];then
        LOG_ERROR "Must set groupId"
        help
    fi
    # check rpcPort
    if [ "${rpc_port}" == "" ];then
        LOG_ERROR "Must set rpcPort"
        help
    fi
}

help()
{
    echo "${1}"
    cat << EOF
Usage:
    -g <group id>               [Required] group id
    -i <rpc listen ip>          [Optional] default is 127.0.0.1
    -p <rpc listen port>        [Required]
    -a <monitor the network of all groups> [Optional] default is false
    -h Help
e.g: 
    bash monitor.sh -a -i 127.0.0.1 -p 30303
    bash monitor.sh -g 1 -i 127.0.0.1 -p 30303
EOF
exit 0
}

execRpcCommand() 
{
    local functionName="${1}"
    if [ "${all_groups}" == true ];then
        rpc_command="curl --insecure -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[],\"id\":83}' http://${ip}:${rpc_port} | jq"
    else
        rpc_command="curl --insecure -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[${group_id}],\"id\":83}' http://${ip}:${rpc_port} | jq"
    fi
    execute_cmd "${rpc_command}"
}

getTotalInfos()
{
    LOG_INFO "============Monitor===================="
    LOG_INFO "# version:"
    execRpcCommand "getClientVersion"
    LOG_INFO "---------------------------------------"
    LOG_INFO "# groupList:"
    execRpcCommand "getGroupList"
    LOG_INFO "---------------------------------------"
    LOG_INFO "# Peers:"
    execRpcCommand "getPeers"
    LOG_INFO "---------------------------------------"
    LOG_INFO "================END===================="
}

getGroupInfos()
{
    LOG_INFO "============Monitor===================="
    LOG_INFO "groupPeers: "
    execRpcCommand "getGroupPeers"
    LOG_INFO "---------------------------------------"
    LOG_INFO "groupBlockNumber: "
    execRpcCommand "getBlockNumber"
    LOG_INFO "---------------------------------------"
    LOG_INFO "totalTransactionCount:"
    execRpcCommand "getTotalTransactionCount"
    LOG_INFO "---------------------------------------"
    LOG_INFO "PBFT view:"
    execRpcCommand "getPbftView"
    LOG_INFO "---------------------------------------"
    LOG_INFO "sync status:"
    execRpcCommand "getSyncStatus"
    LOG_INFO "================END===================="
}

monitorStatus()
{
    if [ "${all_groups}" == "true" ];then
        getTotalInfos
    else
        getGroupInfos
    fi 
}

monitor()
{
    
    while true;do
        monitorStatus
        sleep 5s
    done
}

main()
{
while getopts "g:i:p:ah" option;do
    case ${option} in
    g) group_id=${OPTARG};;
    i) ip=${OPTARG};;
    p) rpc_port=${OPTARG};;
    a) all_groups="true";;
    h) help;;
    esac
done
checkParam
monitor
}
main "$@"
