#!/bin/bash
console_branch="master"
fisco_bcos_path=$(realpath "../build/fisco-bcos-air/fisco-bcos")
build_chain_path=$(realpath "BcosAirBuilder/build_chain.sh")
current_path=$(pwd)
node_list="node0 node1 node2"
check_web3_test="false"

# 各轮测试的端口分配 (p2p_port, rpc_port)
# non-sm:   30300, 20200
# sm:       30400, 20300
# baseline: 30500, 20400
NON_SM_P2P=30300
NON_SM_RPC=20200
SM_P2P=30400
SM_RPC=20300
BASELINE_P2P=30500
BASELINE_RPC=20400

LOG_ERROR() {
    local content=${1}
    echo -e "\033[31m[$(date '+%H:%M:%S')] ${content}\033[0m"
}

LOG_INFO() {
    local content=${1}
    echo -e "\033[32m[$(date '+%H:%M:%S')] ${content}\033[0m"
}

LOG_WARN() {
    local content=${1}
    echo -e "\033[31m[ERROR][$(date '+%H:%M:%S')] ${content}\033[0m"
}

# ============================================================
# 单轮测试函数：在独立目录中完成 init -> expand -> test -> stop
# 参数: round_label, sm_flag, p2p_port, rpc_port, is_baseline
# ============================================================
run_test_round() {
    local ROUND_LABEL=$1
    local SM_FLAG=$2
    local P2P_PORT=$3
    local RPC_PORT=$4
    local IS_BASELINE=$5

    local ROUND_DIR="${current_path}/parallel_${ROUND_LABEL}"
    local NODES_DIR="${ROUND_DIR}/nodes/127.0.0.1"
    local RESULT_FILE="${current_path}/.result_${ROUND_LABEL}"

    mkdir -p "${ROUND_DIR}"
    cd "${ROUND_DIR}"

    echo "0" > "${RESULT_FILE}"

    # ---------- helper: stop nodes in this round ----------
    stop_round_nodes() {
        if [ -d "${NODES_DIR}" ]; then
            cd "${NODES_DIR}"
            if [ -f "stop_all.sh" ]; then
                bash stop_all.sh 2>/dev/null || true
            fi
            cd "${ROUND_DIR}"
        fi
    }

    # ---------- helper: exit with error ----------
    exit_round() {
        local msg=$1
        LOG_ERROR "[${ROUND_LABEL}] ${msg}"
        for node in ${node_list}; do
            LOG_ERROR "[${ROUND_LABEL}] ============ error|warn for ${node} ============="
            cat ${NODES_DIR}/${node}/log/* 2>/dev/null | grep -iE 'error|warn|cons|connectedSize|heart|executor' || true
            LOG_ERROR "[${ROUND_LABEL}] ============ nohup for ${node} ============="
            cat ${NODES_DIR}/${node}/nohup.out 2>/dev/null || true
        done
        stop_round_nodes
        echo "1" > "${RESULT_FILE}"
        # 不在这里 exit，让子shell正常返回
    }

    # ---------- wait_and_start ----------
    wait_and_start_round() {
        cd "${NODES_DIR}"
        LOG_INFO "[${ROUND_LABEL}] Try to start all"
        if [ -z "$(bash start_all.sh 2>&1 | grep 'Exceed waiting time')" ]; then
            LOG_INFO "[${ROUND_LABEL}] Start all success"
        else
            bash stop_all.sh 2>/dev/null || true
            LOG_WARN "[${ROUND_LABEL}] Another testing is running. Waiting 20s and re-try."
            sleep 20
            wait_and_start_round
        fi
        cd "${ROUND_DIR}"
    }

    # ---------- init ----------
    init_round() {
        cd "${ROUND_DIR}"
        echo " ==> fisco-bcos version: "
        ${fisco_bcos_path} -v
        if [ -d "nodes" ]; then
            stop_round_nodes
            rm -rf nodes
        fi
        bash ${build_chain_path} -p ${P2P_PORT},${RPC_PORT} -l "127.0.0.1:3" -e ${fisco_bcos_path} ${SM_FLAG}
        # 启用 web3 rpc (node1)
        sed -e 's/^enable_web3_rpc = false/enable_web3_rpc = true/' -i nodes/127.0.0.1/node1/config.ini

        if [ "${IS_BASELINE}" == "true" ]; then
            perl -p -i -e 's/version=0/version=1/g' nodes/127.0.0.1/node*/config.genesis
            perl -p -i -e 'if (/web3_rpc/) { $flag=1 } elsif ($flag && s/enable\s*=\s*false/enable=true/i) { $flag=0; }' nodes/127.0.0.1/node1/config.ini
        fi

        wait_and_start_round
    }

    # ---------- expand_node ----------
    expand_node_round() {
        local NODE3_P2P=$((P2P_PORT + 3))
        local NODE3_RPC=$((RPC_PORT + 3))

        LOG_INFO "[${ROUND_LABEL}] expand node..."
        cd "${ROUND_DIR}"
        rm -rf config
        mkdir config
        cp -r ${NODES_DIR}/../ca config/
        cp ${NODES_DIR}/node0/config.ini config/
        cp ${NODES_DIR}/node0/config.genesis config/
        cp ${NODES_DIR}/node0/nodes.json config/nodes.json.tmp

        local sed_cmd="sed -i"
        if [ "$(uname)" == "Darwin" ]; then
            sed_cmd="sed -i .bkp"
        fi
        ${sed_cmd} "s/listen_port=${P2P_PORT}/listen_port=${NODE3_P2P}/g" config/config.ini
        ${sed_cmd} "s/listen_port=${RPC_PORT}/listen_port=${NODE3_RPC}/g" config/config.ini
        sed -e "s/\"nodes\":\[/\"nodes\":\[\"127.0.0.1:${NODE3_P2P}\",/" config/nodes.json.tmp > config/nodes.json

        bash ${build_chain_path} -C expand -c config -d config/ca -o nodes/127.0.0.1/node3 -e ${fisco_bcos_path} ${SM_FLAG}
        LOG_INFO "[${ROUND_LABEL}] expand node success, starting node3..."
        bash ${NODES_DIR}/node3/start.sh
        sleep 10

        LOG_INFO "[${ROUND_LABEL}] check expand node status..."
        local flag='false'
        for node in ${node_list}; do
            local count=$(cat ${NODES_DIR}/${node}/log/* 2>/dev/null | grep -i "heartBeat,connected count" | tail -n 1 | awk -F' ' '{print $3}' | awk -F'=' '{print $2}')
            if [ "${count}" == "3" ]; then
                flag='true'
            fi
        done
        if [ "${flag}" == 'true' ]; then
            LOG_INFO "[${ROUND_LABEL}] expand node status normal"
        else
            LOG_ERROR "[${ROUND_LABEL}] expand node status error (continuing anyway)"
        fi
    }

    # ---------- run tests ----------
    run_tests() {
        local SM_PARAM="false"
        if [ "${SM_FLAG}" == "-s" ]; then
            SM_PARAM="true"
        fi

        LOG_INFO "[${ROUND_LABEL}] ======== console_ci_test ========"
        bash ${current_path}/.ci/console_ci_test.sh ${console_branch} "${SM_PARAM}" "${NODES_DIR}"
        if [ $? -ne 0 ]; then
            exit_round "console_integrationTest failed"
            return
        fi
        LOG_INFO "[${ROUND_LABEL}] console_integrationTest success"

        LOG_INFO "[${ROUND_LABEL}] ======== java_sdk_ci_test ========"
        bash ${current_path}/.ci/java_sdk_ci_test.sh ${console_branch} "${SM_PARAM}" "${NODES_DIR}"
        if [ $? -ne 0 ]; then
            exit_round "java_sdk_integrationTest failed"
            return
        fi
        LOG_INFO "[${ROUND_LABEL}] java_sdk_integrationTest success"

        LOG_INFO "[${ROUND_LABEL}] ======== java_sdk_demo_ci_test ========"
        bash ${current_path}/.ci/java_sdk_demo_ci_test.sh ${console_branch} "${SM_PARAM}" "${NODES_DIR}"
        if [ $? -ne 0 ]; then
            exit_round "java_sdk_demo_ci_test failed"
            return
        fi
        LOG_INFO "[${ROUND_LABEL}] java_sdk_demo_ci_test success"
    }

    # ---------- web3 test (仅 baseline) ----------
    run_web3_test() {
        if [ "${check_web3_test}" != "true" ]; then
            return
        fi
        LOG_INFO "[${ROUND_LABEL}] ======== web3_test ========"
        cp ${NODES_DIR}/sdk/* ${current_path}/console/dist/conf/ 2>/dev/null || true
        rm -rf ${current_path}/console/dist/account/ecdsa/*
        cp ${NODES_DIR}/../ca/accounts/* ${current_path}/console/dist/account/ecdsa/ 2>/dev/null || true
        cd "${current_path}/console/dist/"
        local account=$(bash console.sh listAccount 2>/dev/null | grep "current account" | awk -F '(' '{print $1}')
        bash console.sh addBalance "${account}" 200 ether 2>/dev/null || true
        bash console.sh setSystemConfigByKey tx_gas_price 1 2>/dev/null || true
        cd "${current_path}"
        bash ${current_path}/.ci/web3_test.sh "${current_path}/console/dist/account/ecdsa/${account}.pem"
        if [ $? -ne 0 ]; then
            exit_round "web3_test failed"
            return
        fi
        LOG_INFO "[${ROUND_LABEL}] web3 test success"
    }

    # ========================
    # 主流程
    # ========================
    LOG_INFO "======== [${ROUND_LABEL}] start ========"
    init_round

    # 检查是否已经失败
    if [ "$(cat ${RESULT_FILE})" == "1" ]; then
        LOG_ERROR "======== [${ROUND_LABEL}] init failed ========"
        return
    fi

    expand_node_round
    if [ "$(cat ${RESULT_FILE})" == "1" ]; then
        LOG_ERROR "======== [${ROUND_LABEL}] expand failed ========"
        return
    fi

    run_tests
    if [ "$(cat ${RESULT_FILE})" == "1" ]; then
        LOG_ERROR "======== [${ROUND_LABEL}] tests failed ========"
        return
    fi

    if [ "${IS_BASELINE}" == "true" ]; then
        run_web3_test
        if [ "$(cat ${RESULT_FILE})" == "1" ]; then
            LOG_ERROR "======== [${ROUND_LABEL}] web3 test failed ========"
            return
        fi
    fi

    LOG_INFO "======== [${ROUND_LABEL}] ALL PASSED ========"
    stop_round_nodes
    rm -rf "${ROUND_DIR}"
}

# ============================================================
# 主入口
# ============================================================
if [[ -n "${1}" ]]; then
    console_branch=${1}
fi

if [[ -n "${2}" ]]; then
    check_web3_test=${2}
fi

LOG_INFO "=============================================="
LOG_INFO "  Parallel CI Check: non-sm | sm | baseline"
LOG_INFO "=============================================="

# 并行启动三轮测试
run_test_round "non-sm"    ""                ${NON_SM_P2P}  ${NON_SM_RPC}  "false" &
PID_NON_SM=$!
run_test_round "sm"        "-s"              ${SM_P2P}      ${SM_RPC}      "false" &
PID_SM=$!
run_test_round "baseline"  ""                ${BASELINE_P2P} ${BASELINE_RPC} "true" &
PID_BASELINE=$!

LOG_INFO "All 3 rounds launched in parallel (PIDs: non-sm=${PID_NON_SM}, sm=${PID_SM}, baseline=${PID_BASELINE})"
LOG_INFO "Waiting for all rounds to complete..."

# 等待所有后台任务完成
wait ${PID_NON_SM}
RC_NON_SM=$?
wait ${PID_SM}
RC_SM=$?
wait ${PID_BASELINE}
RC_BASELINE=$?

# 收集结果
FAILED_ROUNDS=""
check_round_result() {
    local label=$1
    local result_file="${current_path}/.result_${label}"
    if [ -f "${result_file}" ]; then
        local rc=$(cat "${result_file}")
        if [ "${rc}" != "0" ]; then
            FAILED_ROUNDS="${FAILED_ROUNDS} ${label}"
        fi
        rm -f "${result_file}"
    else
        LOG_WARN "Result file not found for ${label}, treating as failure"
        FAILED_ROUNDS="${FAILED_ROUNDS} ${label}"
    fi
}

check_round_result "non-sm"
check_round_result "sm"
check_round_result "baseline"

# 清理残留的并行目录
rm -rf "${current_path}/parallel_non-sm" "${current_path}/parallel_sm" "${current_path}/parallel_baseline" 2>/dev/null || true

LOG_INFO "=============================================="
if [ -z "${FAILED_ROUNDS}" ]; then
    LOG_INFO "  ALL ROUNDS PASSED!"
    LOG_INFO "=============================================="
    exit 0
else
    LOG_ERROR "  FAILED ROUNDS:${FAILED_ROUNDS}"
    LOG_ERROR "=============================================="
    exit 1
fi