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
    rm -rf nodes
    bash -x ${build_chain_path} -l "127.0.0.1:4" -e ${fisco_bcos_path} "${sm_option}"
    cd nodes/127.0.0.1 && wait_and_start
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
    bash nodes/127.0.0.1/stop_all.sh
    rm -rf nodes
}

if [[ -n "${1}" ]]; then
     console_branch=${1}
fi

# non-sm test
LOG_INFO "======== check non-sm case ========"
init ""
#check_consensus
pwd
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "false" "${current_path}/nodes"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
LOG_INFO "======== check non-sm success ========"

LOG_INFO "======== clear node after non-sm test ========"
clear_node
LOG_INFO "======== clear node after non-sm test success ========"

# sm test
LOG_INFO "======== check sm case ========"
init "-s"
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "true" "${current_path}/nodes"
if [[ ${?} == "0" ]]; then
        LOG_INFO "console_integrationTest success"
    else
        echo "console_integrationTest error"
        exit 1
fi
stop_node
LOG_INFO "======== check sm case success ========"
