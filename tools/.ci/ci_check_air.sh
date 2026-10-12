#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: Air integration test. Phase 1 (native): four chains built with the branch
#             binary start in parallel (nonsm 3 nodes, sm 2, executor-v1 2, auth 2; each
#             expanded by one node) and run the native scenarios from tools/.ci/scenarios/
#             with the fisco-bcos ops subcommands; every chain logs to logs/<chain>.log and
#             the first failure prints that chain's log tail and node error/warn lines.
#             Phase 2 (legacy, RUN_JAVA=true): tools/.ci/java_legacy_air.sh runs the java-sdk
#             suite (its config hard-codes 127.0.0.1:20200) and the web3 Hardhat suite on fresh
#             chains at the default ports; console and java-sdk-demo are already replaced, and
#             the script goes away with the native suite's second phase (spec 2026-10-12 §5.4.4).
#             Local run: FISCO_BCOS_BIN=<binary> CI_PORT_BASE=31300,21200,9545 RUN_JAVA=false \
#                        bash tools/.ci/ci_check_air.sh <branch> <web3:true|false>
#             from an empty working directory; see tools/.ci/README.md.
# @ file    : ci_check_air.sh

ci_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
tools_dir=$(dirname "${ci_dir}")
console_branch="${1:-master}"
check_web3_test="${2:-false}"
# the binary under test; CI builds it at ../build relative to tools/
fisco_bcos_path="${FISCO_BCOS_BIN:-${tools_dir}/../build/fisco-bcos-air/fisco-bcos}"
fisco_bcos_path=$(cd "$(dirname "${fisco_bcos_path}")" && pwd)/$(basename "${fisco_bcos_path}")
build_chain_path="${tools_dir}/BcosAirBuilder/build_chain.sh"
current_path=$(pwd)
# "p2p,rpc,web3" of the nonsm chain; sm/v1/auth add 100 to p2p and rpc, 100 to web3; the
# expanded node adds 3 to p2p/rpc and 1 to web3 (spec §5.4.3)
CI_PORT_BASE="${CI_PORT_BASE:-30300,20200,8545}"
# false skips the Java suites (local runs only: the CI leg keeps them until the native suite
# replaces them)
RUN_JAVA="${RUN_JAVA:-true}"
# local debugging: CI_CHAINS="nonsm auth" runs a subset; CI_KEEP_NODES=true leaves the chains
# running (and their directories in place) after the run instead of clearing them
chains=(${CI_CHAINS:-nonsm sm v1 auth})
CI_KEEP_NODES="${CI_KEEP_NODES:-false}"

LOG_ERROR() { echo -e "\033[31m ${1}\033[0m"; }
LOG_INFO() { echo -e "\033[32m ${1}\033[0m"; }
LOG_WARN() { echo -e "\033[31m[ERROR] ${1}\033[0m"; }

for scenario in "${ci_dir}"/scenarios/*.sh; do
    # shellcheck source=/dev/null
    source "${scenario}"
done

# ---------------------------------------------------------------------------- chain table
chain_index() { case "${1}" in nonsm) echo 0 ;; sm) echo 1 ;; v1) echo 2 ;; auth) echo 3 ;; *) return 1 ;; esac; }
chain_node_count() { case "${1}" in nonsm) echo 3 ;; *) echo 2 ;; esac; }
chain_sm_flag() { [[ "${1}" == "sm" ]] && echo "-s"; }
chain_dir() { echo "${current_path}/nodes/${1}"; }
chain_nodes_dir() { echo "$(chain_dir "${1}")/127.0.0.1"; }
chain_node_dir() { echo "$(chain_nodes_dir "${1}")/node${2}"; }
# chain_ports <chain> → "p2p rpc web3"
chain_ports()
{
    local base=(${CI_PORT_BASE//,/ })
    local offset
    offset=$(( $(chain_index "${1}") * 100 ))
    echo "$(( base[0] + offset )) $(( base[1] + offset )) $(( base[2] + offset ))"
}

# ---------------------------------------------------------------------------- lifecycle
# kills only the fisco-bcos processes started from this working directory's nodes/
stop_chain_processes()
{
    local chain="${1}"
    local nodes_dir
    nodes_dir=$(chain_nodes_dir "${chain}")
    [[ -d "${nodes_dir}" ]] || return 0
    bash "${nodes_dir}/stop_all.sh" >/dev/null 2>&1 || true
    local node
    for node in "${nodes_dir}"/node*; do
        [[ -f "${node}/stop.sh" ]] && bash "${node}/stop.sh" >/dev/null 2>&1 || true
    done
    local pids
    pids=$(pgrep -f "${nodes_dir}/" || true)
    if [[ -n "${pids}" ]]; then
        LOG_WARN "chain ${chain}: killing leftover node processes ${pids}"
        kill -9 ${pids} 2>/dev/null || true
    fi
}

clear_node()
{
    local chain
    for chain in "${chains[@]}"; do
        stop_chain_processes "${chain}"
    done
    # the workflow's earlier "prepare RPCAPI Tests" step (tools/.ci/rpcapi_ci_prepare.sh) leaves
    # four nodes from nodes/127.0.0.1 running on 30300-30303 / 20200-20203 and never stops
    # them; they hold the nonsm chain's ports. Stop them, then kill whatever still runs from
    # this working directory's nodes/ (node command lines carry that path) — never a bare
    # `pkill -f fisco-bcos`, other chains may run on the same machine
    [[ -f "${current_path}/nodes/127.0.0.1/stop_all.sh" ]] && bash "${current_path}/nodes/127.0.0.1/stop_all.sh" >/dev/null 2>&1
    pkill -9 -f "${current_path}/nodes/" 2>/dev/null || true
    rm -rf "${current_path}/nodes" "${current_path}/config"
}

# wait_and_start <nodes_dir>: start_all.sh launches every node with nohup; its own readiness
# check gives up after ~4s, which four chains starting at once on a debug binary exceed, so
# "Exceed waiting time" is not treated as a failure (stopping and restarting a node that is
# still initialising left PBFT in committed_conflict). Instead poll each node for a live
# process and an answering RPC for up to 120s, and only relaunch a node whose process is gone.
wait_and_start()
{
    local nodes_dir="${1}"
    bash "${nodes_dir}/start_all.sh" 2>&1 | grep -v 'Exceed waiting time'
    local waited=0 node pending
    while (( waited < 120 )); do
        pending=""
        for node in "${nodes_dir}"/node*; do
            if ! pgrep -f "${node}/" >/dev/null 2>&1; then
                LOG_WARN "$(basename "${node}") has no process; starting it again"
                bash "${node}/start.sh" 2>&1 | grep -v 'Exceed waiting time'
                pending="${pending} $(basename "${node}")"
            elif ! "${fisco_bcos_path}" attach getBlockNumber --node-dir "${node}" --json >/dev/null 2>&1; then
                pending="${pending} $(basename "${node}")"
            fi
        done
        if [[ -z "${pending}" ]]; then
            LOG_INFO "start_all ${nodes_dir}: every node answers (${waited}s)"
            return 0
        fi
        sleep 3
        waited=$((waited + 3))
    done
    LOG_ERROR "start_all ${nodes_dir}: still waiting for${pending} after ${waited}s"
    return 1
}

# build_chain_for <chain>: build_chain + per-chain genesis/config tweaks. Runs serially for
# the four chains before they start in parallel: concurrent build_chain runs race on
# check_and_install_tassl (the same tgz curl'ed into the same cwd, then moved to
# ~/.fisco/tassl-1.1.1b, a truncated binary persisting) and on /tmp/secp256k1.param
build_chain_for()
{
    local chain="${1}"
    local ports=($(chain_ports "${chain}"))
    local nodes
    nodes=$(chain_node_count "${chain}")
    local -a extra=()
    # build_chain refuses an existing output dir, so the admin key is made beside it and moved in
    local admin_dir="${current_path}/nodes/${chain}-admin"
    if [[ "${chain}" == "auth" ]]; then
        local admin
        admin=$("${fisco_bcos_path}" account new --out "${admin_dir}" --json) || return 1
        admin=$(echo "${admin}" | grep '"address"' | sed -E 's/.*"address"[^"]*"([^"]*)".*/\1/')
        [[ "${admin}" =~ ^0x[0-9a-fA-F]{40}$ ]] || { LOG_ERROR "account new gave no address: ${admin}"; return 1; }
        extra=(-a "${admin}")
        LOG_INFO "chain auth: admin ${admin}"
    fi
    echo " ==> fisco-bcos version: "
    "${fisco_bcos_path}" -v
    bash "${build_chain_path}" -l "127.0.0.1:${nodes}" -p "${ports[0]},${ports[1]}" \
        -o "$(chain_dir "${chain}")" -e "${fisco_bcos_path}" $(chain_sm_flag "${chain}") "${extra[@]}" || return 1
    [[ -d "${admin_dir}" ]] && mv "${admin_dir}" "$(chain_dir "${chain}")/admin"
    local node
    for node in "$(chain_nodes_dir "${chain}")"/node*; do
        # every chain gets its own web3 port; only v1 (web3_test.sh) turns the listener on
        perl -p -i -e 's/listen_port=8545/listen_port='"${ports[2]}"'/g' "${node}/config.ini"
    done
    if [[ "${chain}" == "v1" ]]; then
        perl -p -i -e 's/version=0/version=1/g' "$(chain_nodes_dir "${chain}")"/node*/config.genesis
        perl -p -i -e 'if (/\[web3_rpc\]/) { $flag=1 } elsif ($flag && s/enable\s*=\s*false/enable=true/i) { $flag=0; }' \
            "$(chain_node_dir "${chain}" 0)/config.ini"
    fi
}

# check_consensus <chain>: every node is in consensus. A node that went through a view change
# logs NewViewReached (startup recovery: reachNewView); a fresh chain can sit at view 0 without
# either, so the fallback is the RPC view: getConsensusStatus on the node sees every other
# consensus node connected. Polls up to 90s.
check_consensus()
{
    local chain="${1}"
    LOG_INFO "chain ${chain}: wait for consensus"
    local node waited status connected total
    for node in "$(chain_nodes_dir "${chain}")"/node*; do
        waited=0
        while (( waited < 90 )); do
            if cat "${node}"/log/* 2>/dev/null | grep -qiE 'NewViewReached|reachNewView'; then
                LOG_INFO "chain ${chain}: $(basename "${node}") reached a view (${waited}s)"
                break
            fi
            status=$("${fisco_bcos_path}" attach getConsensusStatus --node-dir "${node}" --json 2>/dev/null || echo '{}')
            connected=$(jq -r '.connectedNodeList // 0' <<<"${status}")
            total=$(jq -r '.consensusNodesNum // 1' <<<"${status}")
            if (( connected >= total - 1 && total > 0 )); then
                LOG_INFO "chain ${chain}: $(basename "${node}") sees ${connected}/${total} consensus nodes (${waited}s)"
                break
            fi
            sleep 3
            waited=$((waited + 3))
        done
        if (( waited >= 90 )); then
            LOG_ERROR "chain ${chain}: $(basename "${node}") not in consensus after ${waited}s; cons lines:"
            cat "${node}"/log/* 2>/dev/null | grep -i cons | tail -n 50
            return 1
        fi
    done
}

# expand_node <chain>: one more node (node<N>) from node0's config, ports +3 / web3 +1, then
# wait until node0 lists it as a peer
expand_node()
{
    local chain="${1}"
    local ports=($(chain_ports "${chain}"))
    local index
    index=$(chain_node_count "${chain}")
    local config="$(chain_dir "${chain}")/expand-config"
    local node0
    node0=$(chain_node_dir "${chain}" 0)
    LOG_INFO "chain ${chain}: expand node${index}"
    rm -rf "${config}"
    mkdir -p "${config}"
    cp -r "$(chain_dir "${chain}")/ca" "${config}/"
    cp "${node0}/config.ini" "${node0}/config.genesis" "${config}/"
    sed -e 's/listen_port='"${ports[0]}"'/listen_port='"$((ports[0] + 3))"'/' \
        -e 's/listen_port='"${ports[1]}"'/listen_port='"$((ports[1] + 3))"'/' \
        -e 's/listen_port='"${ports[2]}"'/listen_port='"$((ports[2] + 1))"'/' \
        "${node0}/config.ini" > "${config}/config.ini"
    sed -e 's/"nodes":\[/"nodes":\["127.0.0.1:'"$((ports[0] + 3))"'",/' "${node0}/nodes.json" > "${config}/nodes.json"
    bash "${build_chain_path}" -C expand -c "${config}" -d "${config}/ca" \
        -o "$(chain_node_dir "${chain}" "${index}")" -e "${fisco_bcos_path}" $(chain_sm_flag "${chain}") || return 1
    bash "$(chain_node_dir "${chain}" "${index}")/start.sh"
    local expected=$(( index ))  # node0 sees every other node, the new one included
    local waited=0 peers
    while (( waited < 60 )); do
        peers=$("${fisco_bcos_path}" attach getPeers --node-dir "${node0}" --json 2>/dev/null | jq '.peers | length' 2>/dev/null || echo 0)
        if (( peers >= expected )); then
            LOG_INFO "chain ${chain}: node${index} connected after ${waited}s (${peers} peers on node0)"
            return 0
        fi
        sleep 2
        waited=$((waited + 2))
    done
    LOG_ERROR "chain ${chain}: node${index} not connected after ${waited}s (${peers} peers on node0)"
    return 1
}

print_chain_failure()
{
    local chain="${1}"
    LOG_ERROR "============= chain ${chain} FAILED: last 200 log lines ============="
    tail -n 200 "${current_path}/logs/${chain}.log"
    local node
    for node in "$(chain_nodes_dir "${chain}")"/node*; do
        [[ -d "${node}" ]] || continue
        LOG_ERROR "============= chain ${chain} $(basename "${node}") error|warn lines ============="
        cat "${node}"/log/* 2>/dev/null | grep -iE 'error|warn' | tail -n 100
        LOG_ERROR "============= chain ${chain} $(basename "${node}") nohup ============="
        cat "${node}/nohup.out" 2>/dev/null | tail -n 50
    done
}

# ---------------------------------------------------------------------------- scenarios
# run_chain <chain>: start the built chain, expand, native scenarios in spec order, stop
run_chain()
{
    local chain="${1}"
    local started=${SECONDS}
    # the patched ABI copies scenarios 2/9 hand to `tx deploy` (scenarios/common.sh contract_abi)
    ci_abi_list=$(mktemp "${TMPDIR:-/tmp}/ci-abi-files-${chain}.XXXXXX") || return 1
    wait_and_start "$(chain_nodes_dir "${chain}")" || return 1
    check_consensus "${chain}" || return 1
    expand_node "${chain}" || return 1
    local node0
    node0=$(chain_node_dir "${chain}" 0)
    # the ops commands talk to node0 over its unix socket (--node-dir), so the RPC port's
    # TLS setting does not matter
    local -a scenarios=(scenario_1_rpc_shape scenario_2_hello scenario_4_block_tx scenario_5_sysconfig
        scenario_8_balance scenario_9_contract_test_all scenario_7_consensus_membership
        scenario_13_viewchange scenario_14_sync)
    [[ "${chain}" == "nonsm" || "${chain}" == "sm" ]] && scenarios+=(scenario_19_account)
    [[ "${chain}" == "nonsm" ]] && scenarios+=(scenario_20_ops_status)
    local scenario number
    for scenario in "${scenarios[@]}"; do
        "${scenario}" "${chain}" "${node0}" || { LOG_ERROR "chain ${chain}: ${scenario} failed"; contract_abi_cleanup; return 1; }
        # CI_FAIL_SCENARIO=<n> (local only) fails the chain after scenario n to exercise the
        # failure report path
        number=${scenario#scenario_}
        number=${number%%_*}
        if [[ "${CI_FAIL_SCENARIO:-}" == "${number}" ]]; then
            LOG_ERROR "chain ${chain}: CI_FAIL_SCENARIO=${number} injected failure"
            return 1
        fi
    done
    contract_abi_cleanup
    [[ "${CI_KEEP_NODES}" == "true" ]] || stop_chain_processes "${chain}"
    LOG_INFO "chain ${chain}: all native scenarios passed in $((SECONDS - started))s"
}

# ---------------------------------------------------------------------------- main
cd "${current_path}"
# the ops commands reach node0 through <node>/data/fisco-bcos.ipc; AF_UNIX allows 104 bytes
if (( ${#current_path} > 50 )); then
    LOG_WARN "working directory is ${#current_path} chars long; the node socket path may exceed the AF_UNIX limit (104). Run from a shorter directory."
fi
clear_node
mkdir -p "${current_path}/logs"
LOG_INFO "======== native scenarios: build ${chains[*]} (serial), then run in parallel (ports from ${CI_PORT_BASE}) ========"
for chain in "${chains[@]}"; do
    build_chain_for "${chain}" > "${current_path}/logs/${chain}.log" 2>&1 || {
        print_chain_failure "${chain}"
        [[ "${CI_KEEP_NODES}" == "true" ]] || clear_node
        LOG_ERROR "======== build_chain for ${chain} FAILED ========"
        exit 1
    }
done
declare -A chain_pid
for chain in "${chains[@]}"; do
    ( run_chain "${chain}" ) >> "${current_path}/logs/${chain}.log" 2>&1 &
    chain_pid[${chain}]=$!
done
failed=()
for chain in "${chains[@]}"; do
    if wait "${chain_pid[${chain}]}"; then
        LOG_INFO "chain ${chain}: ok ($(grep -c '=== scenario' "${current_path}/logs/${chain}.log") scenarios)"
    else
        failed+=("${chain}")
    fi
done
if (( ${#failed[@]} > 0 )); then
    for chain in "${failed[@]}"; do
        print_chain_failure "${chain}"
    done
    [[ "${CI_KEEP_NODES}" == "true" ]] || clear_node
    LOG_ERROR "======== native scenarios FAILED on: ${failed[*]} ========"
    exit 1
fi
for chain in "${chains[@]}"; do
    LOG_INFO "---- logs/${chain}.log (scenario lines) ----"
    grep -E '=== scenario|scenario [0-9]+ ok|all native scenarios' "${current_path}/logs/${chain}.log"
done
[[ "${CI_KEEP_NODES}" == "true" ]] || clear_node
LOG_INFO "======== native scenarios success ========"

if [[ "${RUN_JAVA}" == "true" ]]; then
    LOG_INFO "======== legacy java-sdk suite + web3 (java_legacy_air.sh) ========"
    bash "${ci_dir}/java_legacy_air.sh" "${console_branch}" "${check_web3_test}" || exit 1
fi
