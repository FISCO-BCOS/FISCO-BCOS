#!/bin/bash
dirpath="$(cd "$(dirname "$0")" && pwd)"
cd ${dirpath}

# 群组id
group_id=""
# rpc ip
rpc_ip=""
# rpc 端口
rpc_port=""
#
disk_dir=""

# 区块间隔告警阈值, 共识节点之间的区块高度相差超过height_threshold，说明区块同步或者共识异常
height_threshold=30
# 磁盘容量告警阈值，默认剩余5%开始告警
disk_space_threshold=5

alarm() {
        echo "$1"
        alert_msg="$1"
        alert_ip=$(/sbin/ifconfig eth0 | grep inet | grep -v inet6 | awk '{print $2}')
        alert_time=$(date "+%Y-%m-%d %H:%M:%S")

        # TODO: alarm the message, mail or phone

        # echo "[${alert_time}]:[${alert_ip}]:${alert_msg}"| mail -s "fisco-bcos alarm message" 912554887@qq.com
}

# echo message with time
info() {
        time=$(date "+%Y-%m-%d %H:%M:%S")
        echo "[$time] $1"
}

error() {
        echo -e "\033[31m $1 \033[0m"
}

dir_must_exists() {
    if [ ! -d "$1" ]; then
        error "$1 DIR does not exist, please check!"
        exit 1
    fi
}

function check_disk() {
        local dir="$1"
        local disk_space_left_percent=$(df -h "${dir}" |grep /dev|awk -F" " '{print $5}'|cut -d"%" -f1)
        # info "disk_space_left_percent: ${disk_space_left_percent}, disk_space_threshold:${disk_space_threshold}"
        if [[ ${disk_space_left_percent} -lt ${disk_space_threshold} ]]; then
                alarm " ERROR! insufficient disk capacity, monitor disk directory: ${dir}, left disk space percent: ${disk_space_left_percent}%"
                return 1
        fi
}

# check if nodeX is work well
function check_node_work_properly() {
        local group="${1}"
        local config_ip="${2}"
        local config_port="${3}"
        local node="node"

        ok="true"

        # getBlockNumber
        blocknumberresult=$(curl -s "http://$config_ip:$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getBlockNumber\",\"params\":[\"${group}\", \"\"],\"id\":67}")

        # echo $blocknumberresult
        height=$(echo $blocknumberresult | grep -o "result.*" | egrep -o "[0-9]+")
        [[ -z "$height" ]] && {
                alarm " ERROR! Cannot connect to $config_ip:$config_port ${group}, method: getBlockNumber"
                return 1
        }

        # getConsensusStatus
        consresult=$(curl -s "http://$config_ip:$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getConsensusStatus\",\"params\":[\"${group}\", \"\"],\"id\":67}")
        [[ -z "$consresult" ]] && {
                alarm " ERROR! Cannot connect to $config_ip:$config_port ${group}, method: getConsensusStatus"
                return 1
        }
        # echo ${consresult}
        timeout=$(echo ${consresult} | egrep -o "timeout.*" | awk -F ","  '{ print $1 }' | awk -F ":" '{ print $2 }')
        [[ "${timeout}" == "true" ]] && {
                alarm " ERROR! Consensus timeout $config_ip:$config_port ${group}"
                return 1
        }

        # getSealerList
        nodeids=$(curl -s "http://$config_ip:$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getSealerList\",\"params\":[\"${group}\", \"\"],\"id\":67}" | egrep "nodeID" | awk -F "\"" '{ print $4}' | tr -s "\n" " ")
        [[ -z "$nodeids" ]] && {
                alarm " ERROR! Cannot connect to $config_ip:$config_port ${group}, method: getSealerList"
                return 1
        }
        # echo ${sealerresult}
        nodeids_array=(${nodeids})

        # getSyncStatus
        syncstatusresult=$(curl -s "http://$config_ip:$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getSyncStatus\",\"params\":[\"${group}\", \"\"],\"id\":67}")
        [[ -z "$syncstatusresult" ]] && {
                alarm " ERROR! Cannot connect to $config_ip:$config_port ${group}, method: getSyncStatus"
                return 1
        }

        # check if any node disconnected
        # for nodeid in ${nodeids_array[@]}
        # do
        #        nodeid_exist=$(echo "${syncstatusresult}" | grep -o ${nodeid})
        #        if [[ -z "${nodeid_exist}" ]]; then
        #               alarm " ERROR! The node info is missing in the sync status info, the node may be disconnected, nodeid: ${nodeid} " 
        #               ok="false"
        #               continue
        #        fi
        # done 

        # check if any block number is too far behind
        blocknumbers=$(echo "${syncstatusresult}" | tr -s '\\\"' '"' | egrep -o "blockNumber\":[0-9]+" | awk -F ":" '{ print $2 }' | tr -s "\n" " ")
        blocknumber_array=(${blocknumbers})

        sync_nodeids=$(echo "${syncstatusresult}" | tr -s '\\\"' '"' | tr -s "}" "\n" | tr -s "{" "\n" | egrep -o "nodeID.*" | awk -F '"' '{ print $3}' | tr -s "\n" " ")
        sync_nodeids_array=(${sync_nodeids})

        max_blocknumber=0
        max_node=""
        min_blocknumber=2147483647
        min_node=""
        for((i=0;i<${#blocknumber_array[@]};i++)) do
                blocknumber=${blocknumber_array[i]}
                nodeid=${sync_nodeids_array[i]}
                if [[ ${blocknumber} -ge ${max_blocknumber} ]];then
                        max_blocknumber=${blocknumber}
                        max_node=${nodeid}
                fi

                if [[ ${min_blocknumber} -ge ${blocknumber} ]];then
                        min_blocknumber=${blocknumber}
                        min_node=${nodeid}
                fi
        done;

        ((diff_blocknumber=max_blocknumber-min_blocknumber))

        # 
        if [[ ${diff_blocknumber} -ge "${height_threshold}" ]];then
                ok="false"
                echo "max node: ${max_node}"
                echo "min node: ${min_node}"
                error_msg="[highest block number: ${max_blocknumber}, nodeid: ${max_node}, lowest block number: ${min_blocknumber}, nodeid: ${min_node}]"
                alarm " ERROR! The lowest blocknumber is too far behind the highest block number queried by getSyncStatus, ${error_msg}, content: ${syncstatusresult}"     
        fi

        if [[ "${ok}" == "true" ]];then
             info " OK! $config_ip:$config_port $node:$group is working properly: height $height"
        fi
   
        return 0
}

# check all node of this server, if all node work well.
function check_all_node_work_properly() {
        local ip="${1}"
        local port="${2}"
        local group="${3}"

        if [[ -n "${group}" ]]; then
                check_node_work_properly "${group}" "${ip}" "${port}" 
        else
                # TODO:  be optimized later to support get group list
                # groups=$(curl -s "http://$config_ip:$config_port" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getGroupList\",\"params\":[],\"id\":67}" | ✗ curl -s "http://127.0.0.1:20201" -X POST --data "{\"jsonrpc\":\"2.0\",\"method\":\"getGroupList\",\"params\":[],\"id\":67}" | tr -s "\n" " " | tr -s "\t" " ")
                check_node_work_properly "${group}" "${ip}" "${port}" 
        fi
}

function help() {
        echo "Usage:"
        echo "Optional:"
        echo "    -d                  [Optional]  disk directory to be monitor"
        echo "    -g                  [Require]  group id"
        echo "    -i                  [Require]  rpc server ip"
        echo "    -p                  [Require]  rpc server port"
        echo "    -t                  [Optional] block number far behind warning threshold, default: 30"
        echo "    -T                  [Optional] disk capacity alarm threshold, default: 5%"
        echo "    -h                  Help."
        echo "Example:"
        echo "    bash light_monitor.sh -i 127.0.0.1 -p 20200 -g group0"
        echo "    bash light_monitor.sh -i 127.0.0.1 -p 20200 -g group0 -d /data -T 10"
        exit 0
}

function params_must_set() {
        local name="$1"
        local params="$2"
        local flag="$3"
        [[ -z "${params}" ]] && {
                error "${name} must be set, you can use \'${flag}\' option to set it"
                exit 1
        }
}

while getopts "d:g:i:p:t:T:h" option; do
        case $option in
        d) 
                disk_dir=$OPTARG 
                dir_must_exists ${disk_dir}
                ;;
        g) group_id=$OPTARG ;;
        i) rpc_ip=$OPTARG ;;
        p) rpc_port=$OPTARG ;;
        t) height_threshold=$OPTARG ;;
        T) disk_space_threshold=$OPTARG ;;
        h) help ;;
        esac
done

params_must_set "group id" "${group_id}" "-g"
params_must_set "rpc ip" "${rpc_ip}" "-i"
params_must_set "rpc port" "${rpc_port}" "-p"

# 磁盘容量检查
if [ -n "${disk_dir}" ];then
        check_disk "${disk_dir}"
fi

# 区块链节点状态检查
check_all_node_work_properly  "${rpc_ip}" "${rpc_port}" "${group_id}"
