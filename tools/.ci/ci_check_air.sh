#!/bin/bash
console_branch="master"
# 使用绝对路径，支持在阶段工作目录（AIR_WORKDIR）中运行
fisco_bcos_path="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/build/fisco-bcos-air/fisco-bcos"
build_chain_path="BcosAirBuilder/build_chain.sh"
# AIR_WORKDIR 由并行编排器设置，指向阶段独立工作目录；默认为当前目录
current_path="${AIR_WORKDIR:-$(pwd)}"
cd "${current_path}" || exit 1
node_list="node0"
expanded_node="node3"
check_web3_test="false"
# 导出环境变量给子脚本使用：SKIP_BUILD=跳过下载构建，RUN_DMC=运行DMC转账测试
# 注意：并行编排器会以 SKIP_BUILD=true 启动各阶段，这里不能覆盖继承来的值
export SKIP_BUILD="${SKIP_BUILD:-false}"
export RUN_DMC="false"
# 各阶段端口（set_phase_ports 按阶段隔离，默认保持原值）
p2p_port_start=30300
rpc_port_start=20200
web3_port=8545
export RPC_PORT=${rpc_port_start}
export WEB3_PORT=${web3_port}

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

# 按阶段设置隔离端口，保证三个阶段可以并行运行；
# AIR_PORT_OFFSET 用于本地调试时整体偏移端口，避免与已有节点冲突
set_phase_ports()
{
    local offset=${AIR_PORT_OFFSET:-0}
    case "${1}" in
        non-sm)
            p2p_port_start=$((30300 + offset))
            rpc_port_start=$((20200 + offset))
            web3_port=$((8545 + offset))
            ;;
        sm)
            p2p_port_start=$((31300 + offset))
            rpc_port_start=$((21200 + offset))
            web3_port=$((8555 + offset))
            ;;
        baseline)
            p2p_port_start=$((32300 + offset))
            rpc_port_start=$((22200 + offset))
            web3_port=$((8565 + offset))
            ;;
        *)
            LOG_ERROR "unknown phase: ${1}"
            exit 1
            ;;
    esac
    export RPC_PORT=${rpc_port_start}
    export WEB3_PORT=${web3_port}
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

# 开启 node0 的 web3_rpc 并设置阶段隔离的 web3 端口
enable_web3_rpc()
{
    perl -p -i -e 'if (/\[web3_rpc\]/) { $flag=1 } elsif ($flag && /^\[/) { $flag=0 } if ($flag) { s/enable\s*=\s*false/enable=true/i; s/listen_port=8545/listen_port='"${web3_port}"'/; }' nodes/127.0.0.1/node0/config.ini
}

init()
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -l "127.0.0.1:1" -p "${p2p_port_start},${rpc_port_start}" -e ${fisco_bcos_path} "${sm_option}"
    # enable web3_rpc on node0 config.ini
    enable_web3_rpc
    cd nodes/127.0.0.1 && wait_and_start
}

init_baseline()
{
    sm_option="${1}"
    cd ${current_path}
    echo " ==> fisco-bcos version: "
    ${fisco_bcos_path} -v
    clear_node
    bash ${build_chain_path} -l "127.0.0.1:1" -p "${p2p_port_start},${rpc_port_start}" -e ${fisco_bcos_path} "${sm_option}"

    # 启用executor v1
    # Enable executor v1
    perl -p -i -e 's/version=0/version=1/g' nodes/127.0.0.1/node*/config.genesis
    # enable web3_rpc on node0 config.ini
    enable_web3_rpc
    cd nodes/127.0.0.1 && wait_and_start
}

expand_node()
{
    sm_option="${1}"
    local expanded_p2p_port=$((p2p_port_start + 3))
    local expanded_rpc_port=$((rpc_port_start + 3))
    local expanded_web3_port=$((web3_port + 1))
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
    ${sed_cmd}  "s/listen_port=${p2p_port_start}/listen_port=${expanded_p2p_port}/g" config/config.ini
    ${sed_cmd}  "s/listen_port=${rpc_port_start}/listen_port=${expanded_rpc_port}/g" config/config.ini
    ${sed_cmd}  "s/listen_port=${web3_port}/listen_port=${expanded_web3_port}/g" config/config.ini
    sed -e 's/"nodes":\[/"nodes":\["127.0.0.1:'"${expanded_p2p_port}"'",/' config/nodes.json.tmp > config/nodes.json
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
            if [ "${count}" == "1" ]; then
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
      exit 1
    fi
}

# 检查节点是否已达成共识（出块），测试前健康检查；轮询 reachNewView 替代固定 sleep
check_consensus()
{
    cd ${current_path}/nodes/127.0.0.1
    LOG_INFO "=== wait for the node to reach consensus (polling reachNewView) ====="
    local max_wait=60
    local waited=0
    local all_ok='false'
    while [ $waited -lt $max_wait ]; do
        all_ok='true'
        for node in ${node_list}
        do
            if ! cat ${node}/log/* 2>/dev/null | grep -qi reachN; then
                all_ok='false'
                break
            fi
        done
        if [ "${all_ok}" == 'true' ]; then
            break
        fi
        sleep 2
        waited=$((waited + 2))
    done
    LOG_INFO "=== wait for the node to reach consensus finish (waited ${waited}s) ====="
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
        # 确保本工作目录下的 fisco-bcos 进程确实已退出，防止僵尸进程占用端口；
        # 注意：只能 kill 属于本工作目录的进程，并行阶段之间不能互相误杀
        if [ -d /proc ]; then
            for pid in $(pgrep -f "fisco-bcos" 2>/dev/null); do
                proc_cwd=$(readlink /proc/${pid}/cwd 2>/dev/null)
                case "${proc_cwd}" in
                    "${current_path}"*)
                        LOG_ERROR "Node process ${pid} (cwd ${proc_cwd}) still running after stop, force killing..."
                        kill -9 ${pid} 2>/dev/null || true
                        ;;
                esac
            done
        elif pgrep -f "fisco-bcos" > /dev/null 2>&1; then
            LOG_ERROR "Nodes still running after stop, force killing..."
            pkill -9 -f "fisco-bcos" 2>/dev/null || true
        fi
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
        git fetch --all --depth 1 || { LOG_ERROR "git fetch failed for console"; exit 1; }
        git reset --hard
        rm -rf build log
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        LOG_ERROR "gradlew build failed for console"
        exit 1
    fi
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
        git fetch --all --depth 1 || { LOG_ERROR "git fetch failed for java-sdk"; exit 1; }
        git reset --hard
        rm -rf build log
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        LOG_ERROR "gradlew build failed for java-sdk"
        exit 1
    fi
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
        git fetch --all --depth 1 || { LOG_ERROR "git fetch failed for java-sdk-demo"; exit 1; }
        git reset --hard
        rm -rf build log
        if [ -n "$(git branch -a | grep origin/${console_branch})" ]; then
            git checkout origin/${console_branch}
        else
            git checkout origin/master
        fi
    fi
    bash gradlew build -x test 2>&1 | tail -3
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        LOG_ERROR "gradlew build failed for java-sdk-demo"
        exit 1
    fi
    cd ${current_path}

    export SKIP_BUILD="true"
    LOG_INFO "======== Prebuild dependencies done, SKIP_BUILD=true ========"
}

# ============ non-sm 测试（含扩容）============
run_non_sm()
{
    LOG_INFO "======== check non-sm case ========"
    export RUN_DMC="false"
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
}

# ============ sm 国密测试（扩容、不跑DMC）============
run_sm()
{
    LOG_INFO "======== check sm case ========"
    export RUN_DMC="false"
    init "-s"
    expand_node "-s"
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
}

# ============ baseline 测试（executor v1，含 web3 测试）============
run_baseline()
{
    LOG_INFO "======== check baseline cases ========"
    export RUN_DMC="false"
    init_baseline ""
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
}

# ============ 并行编排：三个阶段在独立工作目录中并行运行 ============
run_parallel()
{
    # 前面步骤（如 RPCAPI 测试）可能在 tools/nodes 留下了运行中的链，先清理，
    # 否则会与非-sm 阶段的默认端口冲突
    clear_node
    # 依赖只需预构建一次，之后硬链接复制到各阶段工作目录（写时复制，不占额外磁盘）
    prebuild_deps

    local base_dir=${current_path}/.air-parallel
    rm -rf ${base_dir}
    mkdir -p ${base_dir}
    local phase_list="non-sm sm baseline"
    local pid_non_sm=""
    local pid_sm=""
    local pid_baseline=""

    for phase in ${phase_list}
    do
        local work_dir=${base_dir}/${phase}
        mkdir -p ${work_dir}
        # 实体复制（不能用硬链接：各阶段会向证书等文件写入不同内容，
        # 共享 inode 会互相污染）
        for dir in .ci BcosAirBuilder console java-sdk java-sdk-demo
        do
            cp -a ${dir} ${work_dir}/${dir}
        done
        # 示例链数据体积大且阶段内用不到，从副本中剔除
        rm -rf ${work_dir}/BcosAirBuilder/nodes ${work_dir}/BcosAirBuilder/nodes.tar.gz
        LOG_INFO "launch phase ${phase}, workdir: ${work_dir}, log: ${base_dir}/${phase}.log"
        AIR_WORKDIR=${work_dir} SKIP_BUILD=true bash ${current_path}/.ci/ci_check_air.sh "${console_branch}" "${check_web3_test}" "${phase}" > ${base_dir}/${phase}.log 2>&1 &
        case "${phase}" in
            non-sm) pid_non_sm=$! ;;
            sm) pid_sm=$! ;;
            baseline) pid_baseline=$! ;;
        esac
    done

    local failed_phases=""
    for phase in ${phase_list}
    do
        local pid=""
        case "${phase}" in
            non-sm) pid=${pid_non_sm} ;;
            sm) pid=${pid_sm} ;;
            baseline) pid=${pid_baseline} ;;
        esac
        if wait ${pid}; then
            LOG_INFO "======== phase ${phase} success ========"
        else
            LOG_ERROR "======== phase ${phase} FAILED, log below ========"
            failed_phases="${failed_phases} ${phase}"
        fi
    done

    if [ -n "${failed_phases}" ]; then
        for phase in ${failed_phases}
        do
            LOG_ERROR "########## log of failed phase ${phase} ##########"
            cat ${base_dir}/${phase}.log
            LOG_ERROR "########## log of failed phase ${phase} end ##########"
        done
        # 失败时保留工作目录便于排查（CI runner 为一次性环境，无磁盘顾虑）
        LOG_ERROR "parallel run failed, phases:${failed_phases}"
        exit 1
    fi

    # 成功时清理工作目录
    rm -rf ${base_dir}
    LOG_INFO "======== all parallel phases success ========"
}

if [[ -n "${1}" ]]; then
     console_branch=${1}
fi

if [[ -n "${2}" ]]; then
     check_web3_test=${2}
fi

# 第3个参数：运行模式
#   all（默认）  三个阶段串行（self-hosted 工作流保持原行为）
#   parallel     三个阶段并行（GitHub-hosted ubuntu runner）
#   non-sm|sm|baseline  只跑单个阶段（并行编排器内部使用，也可单独调试）
mode="${3:-all}"
case "${mode}" in
    all)
        prebuild_deps
        set_phase_ports "non-sm"
        run_non_sm
        set_phase_ports "sm"
        run_sm
        set_phase_ports "baseline"
        run_baseline
        ;;
    parallel)
        run_parallel
        ;;
    non-sm|sm|baseline)
        set_phase_ports "${mode}"
        case "${mode}" in
            non-sm) run_non_sm ;;
            sm) run_sm ;;
            baseline) run_baseline ;;
        esac
        ;;
    *)
        echo "USAGE: ${0} console_branch check_web3_test [all|parallel|non-sm|sm|baseline]"
        exit 1
        ;;
esac
