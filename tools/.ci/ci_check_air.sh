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

exit_node()
{
    cd ${current_path}
    for node in ${node_list}
    do
        LOG_ERROR "exit_node ============= print error|warn info for ${node} ============="
        cat nodes/127.0.0.1/${node}/log/* |grep -iE 'error|warn|cons|connectedSize|heart|executor'
        LOG_ERROR "exit_node ============= print error|warn info for ${node} finish ============="
        LOG_ERROR "exit_node ########### print nohup info for ${node} ###########"
        cat nodes/127.0.0.1/${node}/nohup.out
        LOG_ERROR "exit_node ########### print nohup info for ${node} finish ###########"
    done
    stop_node
    LOG_ERROR "exit_node ######### exit for ${1}"
    exit 1
}

wait_and_start()
{
    LOG_INFO "Try to start all"
    if [ -z "$(bash start_all.sh | grep 'Exceed waiting time')" ]; then
        LOG_INFO "Start all success"
    else
        bash stop_all.sh
        LOG_WARN "Another testing is running. Waiting 20s and re-try to start all."
        sleep 20
        wait_and_start
    fi
}

init()
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -l "127.0.0.1:4" -e ${fisco_bcos_path} "${sm_option}"
    cd nodes/127.0.0.1 && wait_and_start
}

init_baseline()
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -l "127.0.0.1:4" -e ${fisco_bcos_path} "${sm_option}"

    # 将node2、node3替换为baseline scheduler, 这样不一致时可立即发现
    # Replace node2 and node3 with baseline scheduler, so that inconsistencies can be detected immediately
    perl -p -i -e 's/baseline_scheduler=false/baseline_scheduler=true/g' nodes/127.0.0.1/node2/config.ini
    perl -p -i -e 's/baseline_scheduler=false/baseline_scheduler=true/g' nodes/127.0.0.1/node3/config.ini
    perl -p -i -e 's/baseline_scheduler_parallel=false/baseline_scheduler_parallel=true/g' nodes/127.0.0.1/node3/config.ini

    perl -p -i -e 's/level=info/level=trace/g' nodes/127.0.0.1/node1/config.ini
    perl -p -i -e 's/level=info/level=trace/g' nodes/127.0.0.1/node2/config.ini

    cd nodes/127.0.0.1 && wait_and_start
}

expand_node()
{
    sm_option="${1}"
    LOG_INFO "expand node..."
    cd ${current_path}
    rm -rf config
    mkdir config
    cp -r ${current_path}/nodes/ca config/
    cp ${current_path}/nodes/127.0.0.1/node0/config.ini config/
    cp ${current_path}/nodes/127.0.0.1/node0/config.genesis config/
    cp ${current_path}/nodes/127.0.0.1/node0/nodes.json config/nodes.json.tmp
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    ${sed_cmd}  's/listen_port=30300/listen_port=30304/g' config/config.ini
    ${sed_cmd}  's/listen_port=20200/listen_port=20204/g' config/config.ini
    sed -e 's/"nodes":\[/"nodes":\["127.0.0.1:30304",/' config/nodes.json.tmp > config/nodes.json
    cat config/nodes.json
    bash ${build_chain_path} -C expand -c config -d config/ca -o nodes/127.0.0.1/node4 -e ${fisco_bcos_path} "${sm_option}"
    LOG_INFO "expand node success..."
    bash ${current_path}/nodes/127.0.0.1/node4/start.sh
    sleep 10
    LOG_INFO "check expand node status..."
    flag='false'
    for node in ${node_list}
    do
        count=$(cat ${current_path}/nodes/127.0.0.1/${node}/log/* | grep -i "heartBeat,connected count" | tail -n 1 | awk -F' ' '{print $3}' | awk -F'=' '{print $2}')
        if [ ${count} -eq 4 ];then
            flag='true'
        fi
    done
    if [ ${flag} == 'true' ];then
      LOG_INFO "check expand node status normal..."
    else
      LOG_ERROR "check expand node status error..."
    fi
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
    cd ${current_path}
}

clear_node()
{
    cd ${current_path}
    if [ -d "nodes" ]; then
        bash nodes/127.0.0.1/stop_all.sh
        rm -rf nodes
    fi
}

if [[ -n "${1}" ]]; then
     console_branch=${1}
fi

# non-sm test
LOG_INFO "======== check non-sm case ========"
init ""
expand_node ""
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_integrationTest success"
    else
        echo "java_sdk_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_demo_ci_test success"
    else
        echo "java_sdk_demo_ci_test error"
        exit 1
fi
LOG_INFO "======== check non-sm success ========"

LOG_INFO "======== clear node after non-sm test ========"
clear_node
LOG_INFO "======== clear node after non-sm test success ========"

# sm test
LOG_INFO "======== check sm case ========"
init "-s"
expand_node "-s"
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_integrationTest success"
    else
        echo "java_sdk_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_demo_ci_test success"
    else
        echo "java_sdk_demo_ci_test error"
        exit 1
fi

LOG_INFO "======== check sm case success ========"
clear_node
LOG_INFO "======== clear node after sm test success ========"

# baseline暂时不支持balance precompiled，故不测试java_sdk_demo_ci_test
# baseline does not support balance precompiled temporarily, so java_sdk_demo_ci_test is not tested
LOG_INFO "======== check baseline cases ========"
init_baseline "-s"
expand_node "-s"
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} == "0" ]]; then
        LOG_INFO "java_sdk_integrationTest success"
    else
        echo "java_sdk_integrationTest error"
        exit 1
fi
stop_node
LOG_INFO "======== check baseline cases success ========"
clear_node
LOG_INFO "======== clear node after baseline test success ========"