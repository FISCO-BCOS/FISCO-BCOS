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
support_rpc="getBlockNumber getPbftView getConsensusStatus getSyncStatus getClientVersion getPeers getGroupPeers getGroupList getPendingTransactions getTotalTransactionCount getMinerList"
function get_json_value()
{
  local json=$1
  local key=$2

  if [[ -z "$3" ]]; then
    local num=1
  else
    local num=$3
  fi

  local value=$(echo "${json}" | awk -F"[,:}]" '{for(i=1;i<=NF;i++){if($i~/'${key}'\042/){print $(i+1)}}}' | tr -d '"' | sed -n ${num}p)

  echo ${value}
}

execRpcCommand() 
{
    local functionName="${1}"
    if [ "${all_groups}" == true ];then
        rpc_command="curl --fail --silent --show-error -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[],\"id\":83}' http://${ip}:${rpc_port}"
    else
        rpc_command="curl --fail --silent --show-error -X POST --data '{\"jsonrpc\":\"2.0\",\"method\":\"${functionName}\",\"params\":[${group_id}],\"id\":83}' http://${ip}:${rpc_port}"
    fi
    execute_cmd "${rpc_command}"
}

# get block Number
executeRpc()
{
    functionName="${1}"
    LOG_INFO "============${functionName}===================="
    if [ "${functionName}" == "getBlockNumber" ];then
        blockNumberJson=$(execRpcCommand "${functionName}")
        blockNumber=$(get_json_value "${blockNumberJson}" "result" | cut -d '[' -f2 | cut -d ']' -f1)
        blockNumberValue=$(printf "%d\n" "$blockNumber")
        LOG_INFO "BlockNumber:"${blockNumberValue}
    else
        execRpcCommand "${functionName}" | jq
    fi
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
