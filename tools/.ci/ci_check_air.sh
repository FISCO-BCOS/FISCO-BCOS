#!/bin/bash
console_branch="master"
fisco_bcos_path="../build/fisco-bcos-air/fisco-bcos"
build_chain_path="BcosAirBuilder/build_chain.sh"
current_path=`pwd`
node_list="node0 node1"
expanded_node="node3"
check_web3_test="false"
# 导出环境变量给子脚本使用：SKIP_BUILD=跳过下载构建，RUN_DMC=运行DMC转账测试
export SKIP_BUILD="false"
export RUN_DMC="false"
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
    for node in ${node_list} ${expanded_node}
    do
        if [ ! -d "nodes/127.0.0.1/${node}" ]; then
            continue
        fi
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
    bash ${build_chain_path} -l "127.0.0.1:2" -e ${fisco_bcos_path} "${sm_option}"
    # enable web3_rpc on node0 config.ini
    perl -p -i -e 'if (/\[web3_rpc\]/) { $flag=1 } elsif ($flag && s/enable\s*=\s*false/enable=true/i) { $flag=0; }' nodes/127.0.0.1/node0/config.ini
    cd nodes/127.0.0.1 && wait_and_start
}

init_baseline()
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -l "127.0.0.1:2" -e ${fisco_bcos_path} "${sm_option}"

    # 启用executor v1
    # Enable executor v1
    perl -p -i -e 's/version=0/version=1/g' nodes/127.0.0.1/node*/config.genesis
    perl -p -i -e 'if (/web3_rpc/) { $flag=1 } elsif ($flag && s/enable\s*=\s*false/enable=true/i) { $flag=0; }' nodes/127.0.0.1/node0/config.ini
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
    ${sed_cmd}  's/listen_port=30300/listen_port=30303/g' config/config.ini
    ${sed_cmd}  's/listen_port=20200/listen_port=20203/g' config/config.ini
    sed -e 's/"nodes":\[/"nodes":\["127.0.0.1:30303",/' config/nodes.json.tmp > config/nodes.json
    cat config/nodes.json
    bash ${build_chain_path} -C expand -c config -d config/ca -o nodes/127.0.0.1/node3 -e ${fisco_bcos_path} "${sm_option}"
    LOG_INFO "expand node success..."
    bash ${current_path}/nodes/127.0.0.1/node3/start.sh
    # 用轮询替代固定sleep 10，更快检测到节点就绪
    LOG_INFO "Waiting for expanded node to connect (polling)..."
    local max_wait=60
    local waited=0
    flag='false'
    while [ $waited -lt $max_wait ]; do
        for node in ${node_list}
        do
            count=$(cat ${current_path}/nodes/127.0.0.1/${node}/log/* 2>/dev/null | grep -i "heartBeat,connected count" | tail -n 1 | awk -F' ' '{print $3}' | awk -F'=' '{print $2}')
            if [ "${count}" == "2" ]; then
                flag='true'
                break 2
            fi
        done
        sleep 2
        waited=$((waited + 2))
    done
    if [ ${flag} == 'true' ];then
      LOG_INFO "check expand node status normal (waited ${waited}s)..."
    else
      LOG_ERROR "check expand node status error after ${waited}s..."
    fi
}

# 检查节点是否已达成共识（出块），测试前健康检查
check_consensus()
{
    cd ${current_path}/nodes/127.0.0.1
    LOG_INFO "=== wait for the node to reach consensus, waitTime: 20s ====="
    sleep 20
    LOG_INFO "=== wait for the node to reach consensus finish ====="
    for node in ${node_list}
    do
        LOG_INFO "check_consensus for ${node}"
        result=$(cat ${node}/log/* 2>/dev/null | grep -i reachN)
        if [[ -z "${result}" ]]; then
            LOG_ERROR "checkView failed ******* cons info for ${node} *******"
            cat ${node}/log/* 2>/dev/null | grep -i cons
            LOG_ERROR "checkView failed ******* print log info for ${node} finish *******"
            cd ${current_path}
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
        bash nodes/127.0.0.1/stop_all.sh 2>/dev/null
        rm -rf nodes
    fi
    if [ -d "config" ]; then
        rm -rf config
    fi
}

# 预构建所有测试依赖（console/java-sdk/java-sdk-demo），只需执行一次
prebuild_deps()
{
    cd ${current_path}
    LOG_INFO "======== Prebuilding test dependencies (one-time) ========"

    # 预构建 console
    LOG_INFO "Prebuilding console..."
    if [ ! -d "console/.git" ]; then
        rm -rf console
        git clone --depth 1 https://github.com/FISCO-BCOS/console.git
        cd console
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        fi
    else
        cd console
        git fetch --all --depth 1
        git reset --hard
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    cd ${current_path}

    # 预构建 java-sdk
    LOG_INFO "Prebuilding java-sdk..."
    if [ ! -d "java-sdk/.git" ]; then
        rm -rf java-sdk
        git clone --depth 1 https://github.com/FISCO-BCOS/java-sdk.git
        cd java-sdk
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        fi
    else
        cd java-sdk
        git fetch --all --depth 1
        git reset --hard
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    cd ${current_path}

    # 预构建 java-sdk-demo
    LOG_INFO "Prebuilding java-sdk-demo..."
    if [ ! -d "java-sdk-demo/.git" ]; then
        rm -rf java-sdk-demo
        git clone --depth 1 https://github.com/FISCO-BCOS/java-sdk-demo.git
        cd java-sdk-demo
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        fi
    else
        cd java-sdk-demo
        git fetch --all --depth 1
        git reset --hard
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    cd ${current_path}

    export SKIP_BUILD="true"
    LOG_INFO "======== Prebuild dependencies done, SKIP_BUILD=true ========"
}

if [[ -n "${1}" ]]; then
     console_branch=${1}
fi

if [[ -n "${2}" ]]; then
     check_web3_test=${2}
fi

# ============ 阶段1：预构建所有依赖（仅一次）============
prebuild_deps

# ============ 阶段2：non-sm 测试（含扩容 + DMC）============
LOG_INFO "======== check non-sm case ========"
export RUN_DMC="true"
init ""
expand_node ""
check_consensus
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "console_integrationTest error"
    exit 1
fi
LOG_INFO "console_integrationTest success"

bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_integrationTest error"
    exit 1
fi
LOG_INFO "java_sdk_integrationTest success"

# java-sdk-demo 依赖 console 构建产物，串行执行
bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_demo_ci_test error"
    exit 1
fi
LOG_INFO "java_sdk_demo_ci_test success"
LOG_INFO "======== check non-sm success ========"
clear_node

# ============ 阶段3：sm 国密测试（不扩容、不跑DMC）============
LOG_INFO "======== check sm case ========"
export RUN_DMC="false"
init "-s"
check_consensus
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "console_integrationTest error"
    exit 1
fi
LOG_INFO "console_integrationTest success"

bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_integrationTest error"
    exit 1
fi
LOG_INFO "java_sdk_integrationTest success"

bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "true" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_demo_ci_test error"
    exit 1
fi
LOG_INFO "java_sdk_demo_ci_test success"
LOG_INFO "======== check sm case success ========"
clear_node

# ============ 阶段4：baseline 测试（executor v1）============
LOG_INFO "======== check baseline cases ========"
export RUN_DMC="false"
init_baseline ""
check_consensus
bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "console_integrationTest error"
    exit 1
fi
LOG_INFO "console_integrationTest success"

bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_integrationTest error"
    exit 1
fi
LOG_INFO "java_sdk_integrationTest success"

bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "false" "${current_path}/nodes/127.0.0.1"
if [[ ${?} != "0" ]]; then
    echo "java_sdk_demo_ci_test error"
    exit 1
fi
LOG_INFO "java_sdk_demo_ci_test success"

if [[ ${check_web3_test} == "true" ]]; then
    LOG_INFO "======== check web3 test ========"
    cp ${current_path}/nodes/127.0.0.1/sdk/* ${current_path}/console/dist/conf/
    rm -rf ${current_path}/console/dist/account/ecdsa/*
    cp ${current_path}/nodes/ca/accounts/* ${current_path}/console/dist/account/ecdsa/
    cd "${current_path}/console/dist/"
    account=$(bash console.sh listAccount | grep "current account" | awk -F '(' '{print $1}')
    bash console.sh addBalance "${account}" 200 ether
    bash console.sh setSystemConfigByKey tx_gas_price 1
    cd ${current_path}
    bash ${current_path}/.ci/web3_test.sh "${current_path}/console/dist/account/ecdsa/${account}.pem"
    if [[ ${?} == "0" ]]; then
        LOG_INFO "web3 test success"
    else
        echo "web3 test error"
        exit 1
    fi
fi

stop_node
LOG_INFO "======== check baseline cases success ========"
clear_node
LOG_INFO "======== clear node after baseline test success ========"