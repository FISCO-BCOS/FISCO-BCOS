#!/bin/bash
set -e
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
function help()
{
echo "${1}"
    cat << EOF
Usage:
    -g <group id>               [Required] group id
    -n <node list>              [Required] nodes of the given group
    -d <node directory>         [Optional] default is nodes
    -i <node ip>                [Optional] default is 127.0.0.1
    -s <use storage state>      [Optional] default is mpt
    -h Help
e.g: # generate group1 from node0, node1, node2
    bash gen_group.sh -g 2 -d nodes -i 127.0.0.1 -n "0 1 2"
EOF

exit 0
}

group_id=
miner_list=
ip="127.0.0.1"
node_dir=nodes
state_type=mpt
function generate_group_ini()
{
    local nodeid_list="${1}"
    local output="${2}"
    cat << EOF > ${output}
;consensus configuration
[consensus]
;consensus type: only support PBFT now
consensusType=pbft
;the max number of transactions of a block
maxTransNum=1000
;the node id of leaders
$nodeid_list

;sync period time
[sync]
idleWaitMs=200

[storage]
;storage db type, now support leveldb 
type=LevelDB
dbpath=data

[state]
;state type, now support mpt/storage
type=${state_type}



;genesis configuration
[genesis]
;used to mark the genesis block of this group
;mark=${group_id}

;txpool limit
[txPool]
limit=1000
EOF
}

function updateConfig()
{
    local configFile="${1}"
    local config="${2}"
    sed -i "/\[group\]/a$config" ${configFile} 
}

function generateGroupConfig()
{
    local groupId="${1}"
    local prefix="${2}"
    local minerList="${3}"
    read -a miners <<< ${minerList}
    for minerNode in ${miners[*]};do
        local nodeidList=""
        i=0
        if [ ! -d "${prefix}_${minerNode}" ];then
            LOG_ERROR "Directory ${prefix}_${minerNode} not exists!"
            continue
        fi
        for miner in ${miners[*]};do
            if [ ! -f "${prefix}_${miner}/config.ini" ];then
                LOG_ERROR "${prefix}_${miner}/config.ini not exists!"
                continue
            fi
            certDir=`cat ${prefix}_${miner}/config.ini | grep data_path | grep -v ";" | cut -d'=' -f2`
            if [ "${certDir}" == "" ];then
                certDir="conf/"
            fi
            minerNodeId=`cat ${prefix}_${miner}/${certDir}/node.nodeid`
            if [ ! -f "${prefix}_${miner}/${certDir}/node.nodeid" ];then
                LOG_ERROR "${prefix}_${miner}/${certDir}/node.nodeid not exists!"
                continue
            fi
            nodeidList="${nodeidList}node.${i}=${minerNodeId}"$'\n'
            i=$((i+1))
        done
        groupConfigPath=`cat ${prefix}_${minerNode}/config.ini | grep group_config.${group_id} | grep -v ";" | cut -d'=' -f2`
        if [ "${groupConfigPath}" == "" ];then
            groupConfigPath=conf/
            mkdir -p ${groupConfigPath}
            updateConfig "${prefix}_${minerNode}/config.ini" "    group_config.${groupId}=conf/group.${groupId}.ini"
            groupConfigPath=${groupConfigPath}"/group."${groupId}".ini"
        fi
        groupConfigPath=${prefix}_${minerNode}/${groupConfigPath}
        generate_group_ini "${nodeidList}" "${groupConfigPath}"
    done
}

function checkParam()
{
    if [ "${group_id}" == "" ];then
        LOG_ERROR "Must set group id"
        exit 0
    fi
    if [ "${miner_list}" == "" ];then
        LOG_ERROR "Must set Miner list"
        exit 0
    fi
    if [ "${prefix}" == "" ];then
        LOG_ERROR "Must set prefix"
        exit 0
    fi
}

function main()
{
while getopts "g:n:d:i:sh" option;do
    case ${option} in
    g) group_id=${OPTARG};;
    n) miner_list=${OPTARG};;
    d) node_dir=${OPTARG};;
    i) ip=${OPTARG};;
    s) state_type=storage;;
    h) help;;
    esac
done
prefix=${node_dir}"/node_${ip}"
checkParam
generateGroupConfig "${group_id}" "${prefix}" "${miner_list}"
}
main "$@"