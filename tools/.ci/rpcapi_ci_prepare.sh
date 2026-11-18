#!/bin/bash
# @ function: prepare the chain the RPCAPI newman collection runs against: build a four-node
#             AIR chain with the freshly built binary, start it, and leave it running (the next
#             workflow step talks to it on 20200).
#
#             Readiness is decided by the node answering RPC, not by start_all.sh's own report.
#             start.sh gives up after ~3.5s ("Exceed waiting time") and four nodes initialising
#             concurrently on a CI runner exceed that: the old script treated the string as
#             "another test is running", ran stop_all.sh -- killing the nodes it had just
#             launched mid-initialisation -- and recursed without a bound, so a slow runner
#             never started the chain and never said why.
# @ file    : rpcapi_ci_prepare.sh
set -u

current_path=$(pwd)
build_chain_path="BcosAirBuilder/build_chain.sh"
nodes_dir="${current_path}/nodes/127.0.0.1"
# absolute: every helper below reads it from a different working directory
fisco_bcos_path=$(cd "${current_path}/../build/fisco-bcos-air" && pwd)/fisco-bcos

# The node prints "The fisco-bcos is running..." only after initializer->init() finishes
# (config, RocksDB, genesis), so this is a startup budget, not a request timeout. It is enforced
# against wall time for the round, relaunches included.
START_TIMEOUT=${START_TIMEOUT:-120}
# Worst case wall time one generated start.sh spends before giving up: `sleep 1.5` plus four
# `sleep 0.5` retries (build_chain.sh). Relaunching is synchronous, so it is charged against the
# round's remaining budget before it is spent.
START_SCRIPT_BUDGET=${START_SCRIPT_BUDGET:-4}
# Only nodes whose process is gone are relaunched, so one round is normally enough; the bound
# exists so a genuinely broken node fails this step instead of looping until the job times out.
MAX_ROUNDS=${MAX_ROUNDS:-3}

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

rpc_answers()
{
    "${fisco_bcos_path}" attach getBlockNumber --node-dir "${1}" --json >/dev/null 2>&1
}

# diagnose_node <node_dir>: say why the node is not serving, instead of one generic line.
diagnose_node()
{
    local node="${1}"
    LOG_ERROR "$(basename "${node}") is not serving; nohup.out tail:"
    tail -n 20 "${node}/nohup.out" 2>/dev/null | sed 's/^/    /'
    if grep -q 'Address already in use' "${node}/nohup.out" 2>/dev/null; then
        LOG_ERROR "  cause: a listen port is taken -- another chain, or nodes left running by an earlier run"
    elif grep -q 'InvalidConfig' "${node}/nohup.out" 2>/dev/null; then
        LOG_ERROR "  cause: invalid configuration, see the message above"
    elif grep -q 'Too many open files' "${node}/nohup.out" 2>/dev/null; then
        LOG_ERROR "  cause: fd limit too low (start.sh raises it to 1024)"
    fi
}

# wait_for_nodes: every node has a live process AND answers RPC. Relaunching is limited to nodes
# whose process is gone -- restarting one that is still initialising resets its storage setup.
#
# The budget is wall time for the whole round, not a count of the sleeps below. A relaunch is
# synchronous and costs up to START_SCRIPT_BUDGET even when the node fails instantly, so what is
# left is checked before every launch and probe. Counting only `sleep 3` used to let a round run
# far past its stated budget: four instantly-failing nodes cost 4 x 3.5s of start.sh plus the 3s
# sleep (17s) while the counter advanced by 3s, so START_TIMEOUT=120 ran for ~680s and logged
# 120s -- and MAX_ROUNDS=3 then spent ~34 minutes before giving up.
wait_for_nodes()
{
    local started=${SECONDS} deadline=$(( SECONDS + START_TIMEOUT ))
    local node pending remaining elapsed
    while :; do
        pending=""
        for node in "${nodes_dir}"/node*; do
            [ -d "${node}" ] || continue
            remaining=$(( deadline - SECONDS ))
            if ! pgrep -f "${node}/" >/dev/null 2>&1; then
                if [ "${remaining}" -lt "${START_SCRIPT_BUDGET}" ]; then
                    # Not enough left to relaunch and still learn anything useful: report the
                    # node instead of spending the budget on a launch that cannot be evaluated.
                    pending="${pending} $(basename "${node}")"
                    continue
                fi
                LOG_WARN "$(basename "${node}") has no process; starting it again"
                bash "${node}/start.sh" 2>&1 | grep -v 'Exceed waiting time'
                pending="${pending} $(basename "${node}")"
            elif [ "${remaining}" -le 0 ] || ! rpc_answers "${node}"; then
                # Out of budget: the probe would overrun, so the node is unresolved, not healthy.
                pending="${pending} $(basename "${node}")"
            fi
        done
        elapsed=$(( SECONDS - started ))
        if [ -z "${pending}" ]; then
            LOG_INFO "every node answers (${elapsed}s)"
            return 0
        fi
        remaining=$(( deadline - SECONDS ))
        [ "${remaining}" -le 0 ] && break
        # never sleep past the deadline
        if [ "${remaining}" -lt 3 ]; then sleep "${remaining}"; else sleep 3; fi
    done
    elapsed=$(( SECONDS - started ))
    LOG_ERROR "still not serving after ${elapsed}s:${pending}"
    for node in ${pending}; do
        diagnose_node "${nodes_dir}/${node}"
    done
    return 1
}

stop_nodes()
{
    [ -f "${nodes_dir}/stop_all.sh" ] && bash "${nodes_dir}/stop_all.sh" >/dev/null 2>&1
    # the stop scripts only know the pid of the node they were generated for; a node that is
    # still initialising may outlive it, and its command line carries this directory
    pkill -9 -f "${nodes_dir}/" 2>/dev/null
    return 0
}

# teardown_on_failure: on success the chain must stay up for the newman step that follows. On
# failure it must not: it would hold 30300-30303 / 20200-20203 for whatever runs next.
teardown_on_failure()
{
    local rc=$?
    if [ "${rc}" -ne 0 ]; then
        LOG_WARN "prepare failed (rc=${rc}); stopping the chain so it cannot hold ports for the next step"
        stop_nodes
    fi
    exit "${rc}"
}

start_chain()
{
    local started=${SECONDS} round=1
    while [ "${round}" -le "${MAX_ROUNDS}" ]; do
        LOG_INFO "Try to start all (attempt ${round}/${MAX_ROUNDS})"
        # "Exceed waiting time" is start_all.sh's own 3.5s report and is not a verdict: this
        # function checks the real condition (process alive + RPC answering) below.
        bash "${nodes_dir}/start_all.sh" 2>&1 | grep -v 'Exceed waiting time'
        if wait_for_nodes; then
            LOG_INFO "Start all success"
            LOG_INFO "Waiting connection established"
            sleep 20
            return 0
        fi
        LOG_WARN "chain not serving after attempt ${round}/${MAX_ROUNDS}; stopping it and retrying"
        stop_nodes
        round=$((round + 1))
        # no point waiting before the round that will not happen
        [ "${round}" -le "${MAX_ROUNDS}" ] && sleep 10
    done
    LOG_ERROR "chain did not start after ${MAX_ROUNDS} attempts ($(( SECONDS - started ))s total)"
    return 1
}

init()
{
    cd "${current_path}" || exit 1
    echo " ==> fisco-bcos version: "
    "${fisco_bcos_path}" -v
    clear_node
    # A build failure leaves the node directories half-populated (no config.ini/config.genesis),
    # which start.sh can only report as "config 'config.ini' not found" -- say it here instead.
    if ! bash "${build_chain_path}" -p 30300,20200 -l "127.0.0.1:4" -e "${fisco_bcos_path}"; then
        LOG_ERROR "build_chain.sh failed; no chain to test"
        return 1
    fi
    open_disablessl
    start_chain
}

stop_node()
{
    cd "${current_path}" || exit 1
    LOG_INFO "exit_node >>>>>>> stop all nodes <<<<<<<<<<<"
    # stop_all.sh's own report is a 3.5s window like start.sh's, so it is not a verdict either:
    # stop_nodes escalates to the nodes whose pid the scripts no longer know.
    stop_nodes
    LOG_INFO "Stop success"
}

clear_node()
{
    cd "${current_path}" || exit 1
    if [ -d "nodes" ]; then
        stop_nodes
        rm -rf nodes
    fi
}

open_disablessl(){
    cd "${nodes_dir}/node0" || exit 1
    local sed_cmd="sed -i"
    if [ "$(uname)" == "Darwin" ];then
        sed_cmd="sed -i .bkp"
    fi
    ${sed_cmd}  's/enable_ssl=true/enable_ssl=false/g' config.ini
    cd "${current_path}" || exit 1
}

trap teardown_on_failure EXIT
init
