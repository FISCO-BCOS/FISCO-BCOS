#!/bin/bash
console_branch="release-3.0.0-rc4"
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

stop_node()
{
    cd ${current_path}
    LOG_INFO "exit_node >>>>>>> stop all nodes <<<<<<<<<<<"
    bash nodes/127.0.0.1/stop_all.sh
}
exit_node()
{
    cd ${current_path}
    for node in ${node_list}
    do
        LOG_ERROR "exit_node ============= print error|warn info for ${node} ============="
        cat nodes/127.0.0.1/${node}/log/* |grep -iE 'error|warn|cons|connectedSize|heart'
        LOG_ERROR "exit_node ============= print error|warn info for ${node} finish ============="
        LOG_ERROR "exit_node ########### print nohup info for ${node} ###########"
        cat nodes/127.0.0.1/${node}/nohup.out
        LOG_ERROR "exit_node ########### print nohup info for ${node} finish ###########"
    done
    stop_node
    LOG_ERROR "exit_node ######### exit for ${1}"
    exit 1
}

init() 
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    bash ${build_chain_path} -l "127.0.0.1:4" -e ${fisco_bcos_path} "${sm_option}"
    cd nodes/127.0.0.1 && bash start_all.sh
}

check_consensus()
{
    cd ${current_path}/nodes/127.0.0.1
    LOG_INFO "=== wait for the node to init, waitTime: 20s ====="
    sleep 20
    LOG_INFO "=== wait for the node to init finish ====="
    for node in ${node_list}
    do
        LOG_INFO "check_consensus for ${node}"
        result=$(cat ${node}/log/* |grep -i reachN)
        if [[ -z "${result}" ]];
        then
            LOG_ERROR "checkView failed ******* cons info for ${node} *******"
            cat ${node}/log/* |grep -i cons
            LOG_ERROR "checkView failed ******* print log info for ${node} finish *******"
            exit_node "check_consensus for ${node} failed for not reachNewView"
        else
            LOG_INFO "check_consensus for ${node} success"  
        fi 
    done
}

download_console()
{
    cd ${current_path}
    LOG_INFO "Download console ..."
    git clone https://github.com/FISCO-BCOS/console && cd console && git checkout ${console_branch} 
    LOG_INFO "Download console success, branch: ${console_branch}"
    LOG_INFO "Build and Config console ..."
    bash gradlew build -x test && cd dist
}

config_console()
{
    cd ${current_path}/console/dist
    use_sm="${1}"
    cp -r ${current_path}/nodes/127.0.0.1/sdk/* conf/
    cp conf/config-example.toml conf/config.toml
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    use_sm_str="useSMCrypto = \"${use_sm}\""
    ${sed_cmd} "s/useSMCrypto = \"false\"/${use_sm_str}/g" conf/config.toml
    LOG_INFO "Build and Config console success ..."
}

send_transactions()
{
    txs_num="${1}"
    cd ${current_path}/console/dist
    LOG_INFO "Deploy HelloWorld..."
    for((i=1;i<=${txs_num};i++));
    do
        bash console.sh deploy HelloWorld
        sleep 1
    done  
    blockNumber=`bash console.sh getBlockNumber`
    if [ "${blockNumber}" -ne "${txs_num}" ]; then
        exit_node "send transaction failed, current blockNumber: ${blockNumber}"
    else
        LOG_INFO "send transaction success, current blockNumber: ${blockNumber}"
    fi
}

check_sync()
{
    LOG_INFO "check sync..."
    expected_block_number="${1}"
    cd ${current_path}/nodes/127.0.0.1
    bash node0/stop.sh && rm -rf node0/log && rm -rf node0/data
    bash node0/start.sh
    # wait for sync
    sleep 10
    block_number=$(cat node0/log/* |grep Report | tail -n 1| awk -F',' '{print $4}' | awk -F'=' '{print $2}')
    if [ "${block_number}" -ne "${expected_block_number}" ]; then
        exit_node "check_sync error, current blockNumber: ${block_number}, expected_block_number: ${expected_block_number}"
    else
        LOG_INFO "check_sync success, current blockNumber: ${block_number}"
    fi
    LOG_INFO "check sync success..."
}

clear_node()
{
    cd ${current_path}
    bash nodes/127.0.0.1/stop_all.sh
    rm -rf nodes
}

txs_num=10
# non-sm test
LOG_INFO "======== check non-sm case ========"
init ""
check_consensus
download_console
config_console "false"
send_transactions ${txs_num}
check_sync ${txs_num}
LOG_INFO "======== check non-sm success ========"

LOG_INFO "======== clear node after non-sm test ========"
clear_node
LOG_INFO "======== clear node after non-sm test success ========"

# sm test
LOG_INFO "======== check sm case ========"
init "-s"
check_consensus
config_console "true"
send_transactions ${txs_num}
check_sync ${txs_num}
stop_node
LOG_INFO "======== check sm case success ========"