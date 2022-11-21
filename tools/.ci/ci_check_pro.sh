#!/bin/bash
console_branch="3.0.0"
fisco_bcos_service_path="../build/fisco-bcos-tars-service/"
build_chain_path="BcosBuilder/pro/build_chain.py"
current_path=`pwd` # tools
node_list="group0_node_40402 group0_node_40412"
output_dir="pro_nodes"
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

    LOG_INFO "exit_node >>>>>>> stop all pro nodes <<<<<<<<<<<"
    bash ${output_dir}/127.0.0.1/stop_all.sh
}

exit_node()
{
    cd ${current_path}
    for node in ${node_list}
    do
        LOG_ERROR "exit_node ============= print error|warn info for ${node} ============="
        cat ${output_dir}/127.0.0.1/${node}/log/* |grep -iE 'error|warn|cons|connectedSize|heart'
        LOG_ERROR "exit_node ============= print error|warn info for ${node} finish ============="
        LOG_ERROR "exit_node ########### print nohup info for ${node} ###########"
        cat ${output_dir}/127.0.0.1/${node}/nohup.out
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

    echo "===>> ${current_path}"

    rm -rf BcosBuilder/pro/binary/

    mkdir -p BcosBuilder/pro/binary/
    # copy service binary
    mkdir -p BcosBuilder/pro/binary/BcosNodeService
    mkdir -p BcosBuilder/pro/binary/BcosGatewayService
    mkdir -p BcosBuilder/pro/binary/BcosRpcService

    cp -f ${fisco_bcos_service_path}/NodeService/pro/BcosNodeService BcosBuilder/pro/binary/BcosNodeService
    cp -f ${fisco_bcos_service_path}/GatewayService/main/BcosGatewayService BcosBuilder/pro/binary/BcosGatewayService
    cp -f ${fisco_bcos_service_path}/RpcService/main/BcosRpcService BcosBuilder/pro/binary/BcosRpcService
    rm -rf ${output_dir}

    python3 --version
    pip3 --version
    pip3 install -r BcosBuilder/requirements.txt

    cd BcosBuilder/pro/
    python3  build_chain.py build -c conf/config-build-example.toml -O ${current_path}/${output_dir}
    cd ${current_path}

    cd ${output_dir}/127.0.0.1 && bash start_all.sh
}

check_consensus()
{
    cd ${current_path}/${output_dir}/127.0.0.1
    LOG_INFO "=== wait for the node to init, waitTime: 20s ====="
    sleep 20
    LOG_INFO "=== wait for the node to init finish ====="
    for node in ${node_list}
    do
        LOG_INFO "check_consensus for ${node}"
        result=$(cat ${node}/log/*log |grep -i reachN)
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
    tar_file=console-${console_branch}.tar.gz
    if [ -f "${tar_file}" ]; then
        LOG_INFO "Use download cache"
    else
        curl -#LO https://osp-1257653870.cos.ap-guangzhou.myqcloud.com/FISCO-BCOS/console/releases/v${console_branch}/console.tar.gz
        LOG_INFO "Download console success, branch: ${console_branch}"
        mv console.tar.gz ${tar_file}
    fi
    LOG_INFO "Build and Config console ..."
    rm -rf console
    tar -zxvf ${tar_file}
    cd console
}

config_console()
{
    cd ${current_path}/console/
    use_sm="${1}"
    cp -r ${current_path}/${output_dir}/127.0.0.1/rpc_20200/conf/sdk/* conf/
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
    cd ${current_path}/console/
    LOG_INFO "Deploy HelloWorld..."
    for((i=1;i<=${txs_num};i++));
    do
        bash console.sh deploy HelloWorld
        sleep 1
    done  
    blockNumber=`bash console.sh getBlockNumber`
    if [ "${blockNumber}" == "${txs_num}" ]; then
        LOG_INFO "send transaction success, current blockNumber: ${blockNumber}"
    else
        exit_node "send transaction failed, current blockNumber: ${blockNumber}"
    fi
}

check_sync()
{
    LOG_INFO "check sync..."
    expected_block_number="${1}"
    cd ${current_path}/${output_dir}/127.0.0.1
    bash group0_node_40402/stop.sh && rm -rf group0_node_40402/log && rm -rf group0_node_40402/group0
    bash group0_node_40402/start.sh
    # wait for sync
    sleep 10
    block_number=$(cat group0_node_40402/log/*log |grep Report | tail -n 1| awk -F',' '{print $4}' | awk -F'=' '{print $2}')
    if [ "${block_number}" == "${expected_block_number}" ]; then
        LOG_INFO "check_sync success, current blockNumber: ${block_number}"
    else
        exit_node "check_sync error, current blockNumber: ${block_number}, expected_block_number: ${expected_block_number}"
    fi
    LOG_INFO "check sync success..."
}

clear_node()
{
    cd ${current_path}
    bash ${output_dir}/127.0.0.1/stop_all.sh
    rm -rf ${output_dir}
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
stop_node
LOG_INFO "======== check non-sm success ========"

# TODO: support sm test
# LOG_INFO "======== check sm case ========"
# init "-s"
# check_consensus
# config_console "true"
# send_transactions ${txs_num}
# check_sync ${txs_num}
# stop_node
# LOG_INFO "======== check sm case success ========"