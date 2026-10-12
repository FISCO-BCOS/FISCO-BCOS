#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 20 ops-status: the built-in operator commands run beside the chain —
#             status, tx smoke, log status (the node's own log files agree with the RPC
#             snapshot on height) and tui --version-check. Existing ops_tool_test.
# @ file    : ops.sh

scenario_20_ops_status()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 20 ops-status "existing ops_tool_test"
    local status smoke logStatus
    smoke=$(ops "${node_dir}" tx smoke) || return 1
    assert_jq "${smoke}" '.ok == true' "tx smoke ok" || return 1
    status=$(ops "${node_dir}" status) || return 1
    assert_jq "${status}" '[.checks[] | .ok] | all' "status checks ok" || return 1
    # log status exits 2 because four of its checks are "skipped" (the log snapshot lacks the
    # txpool/sync/peer fields); the assertions below are on its content
    logStatus=$(ops "${node_dir}" log status) || { (( $? == 2 )) || return 1; }
    assert_jq "${logStatus}" '.source == "log"' "log status reads the log files" || return 1
    # consensus_stable is not asserted from the log: an idle chain triggers a consensus-timeout
    # view change every few seconds (ViewChangeTriggered → NewViewReached), so the log snapshot
    # may be read in between; the RPC snapshot above already checked it
    assert_jq "${logStatus}" '.chain.blockNumber | type == "number"' "log status has a height" || return 1
    local rpcHeight logHeight
    rpcHeight=$(ops "${node_dir}" status | jq -r '.chain.blockNumber')
    logHeight=$(jq_get "${logStatus}" '.chain.blockNumber')
    # the log is written when the block is committed; it may trail the RPC by the block in flight
    if (( rpcHeight - logHeight > 2 || logHeight > rpcHeight )); then
        scenario_fail "log status height ${logHeight} vs rpc ${rpcHeight}"
        return 1
    fi
    "${fisco_bcos_path}" tui --version-check || { scenario_fail "tui --version-check"; return 1; }
    echo "scenario 20 ok: status/smoke/log status/tui --version-check on ${chain}"
}
