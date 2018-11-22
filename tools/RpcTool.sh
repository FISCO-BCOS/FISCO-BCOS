#!/bin/bash
LOG_ERROR()
{
    content=${1}
    echo -e "\033[31m"${content}"\033[0m"
}

LOG_INFO()
{
    content=${1}
    echo -e "\033[32m"${content}"\033[0m"
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
function_name=
support_rpc="getBlockNumber getPbftView getConsensusStatus getSyncStatus getClientVersion getPeers getGroupPeers getGroupList getPendingTransactions getTotalTransactionCount"
execRpcCommand() 
{
    local functionName="${1}"
    if [ "${all_groups}" == true ];then
        rpc_command="curl -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[],\"id\":83}' http://${ip}:${rpc_port} | jq"
    else
        rpc_command="curl -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[${group_id}],\"id\":83}' http://${ip}:${rpc_port} | jq"
    fi
    execute_cmd "${rpc_command}"
}

# get block Number
executeRpc()
{
    functionName="${1}"
    LOG_INFO "============${functionName}===================="
    execRpcCommand "${functionName}"
}
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
    # check function name
    if [ "${function_name}" == "" ];then
        LOG_ERROR "Must set function name"
        help
    fi
    
    result=`echo ${support_rpc} | grep -w ${function_name}`
    if [ "${result}" == "" ];then
        LOG_ERROR "Not support "${function_name}" yet!"
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
    -f <interface name of rpc>  [Required] eg. getBlockNumber
    (now support [${support_rpc}])
    -h Help
e.g: 
    bash RpcTool.sh -a -i 127.0.0.1 -p 30303 -f getPeers
    bash RpcTool.sh -g 1 -i 127.0.0.1 -p 30303 -f getBlockNumber
EOF
exit 0
}

main()
{
while getopts "g:i:p:f:ah" option;do
    case ${option} in
    g) group_id=${OPTARG};;
    i) ip=${OPTARG};;
    p) rpc_port=${OPTARG};;
    a) all_groups="true";;
    f) function_name=${OPTARG};;
    h) help;;
    esac
done
checkParam
executeRpc "${function_name}"
}
main "$@"
