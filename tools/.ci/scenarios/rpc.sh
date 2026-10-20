#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 1 rpc-shape: the status snapshot runs its five checks and the read
#             RPCs the console client test called answer with the expected JSON types.
#             Replaces console ConsoleClientTest.clientNotParamsTest (lines 23-94).
# @ file    : rpc.sh

scenario_1_rpc_shape()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 1 rpc-shape "console ConsoleClientTest.clientNotParamsTest:23-94"
    # a fresh idle chain reports consensus_stable/height_advancing as failed until its first
    # block (the startup view change never completes without a transaction): warm it up
    local warm
    warm=$(ops "${node_dir}" tx smoke --timeout 60000) || return 1
    assert_jq "${warm}" '.ok == true' "warm-up tx smoke" || return 1
    local status rc
    status=$(ops "${node_dir}" status) && rc=0 || rc=$?
    assert_jq "${status}" '.checks | length == 5' "status has five checks" || return 1
    assert_checks_but_view_change "${status}" "${rc}" "status: every check ok (quorum_connected info) but a view-change consensus_stable" "ok info" || return 1
    assert_jq "${status}" '.chain.blockNumber | type == "number"' "status chain.blockNumber" || return 1
    assert_jq "${status}" '.identity.nodeID | length > 0' "status identity.nodeID" || return 1
    local group
    group=$(jq_get "${status}" '.identity.groupId')

    # method → jq assertion on its result (attach fills [group, ""] in for group methods)
    local -a methods=(
        'getBlockNumber|type == "number" and . >= 0'
        'getPbftView|type == "number" and . >= 0'
        'getSealerList|type == "array" and length > 0 and (.[0].nodeID | length > 0)'
        'getObserverList|type == "array"'
        'getSyncStatus|.blockNumber | type == "number"'
        'getConsensusStatus|.consensusNodeList | length > 0'
        'getPeers|.p2pNodeID | length > 0'
        'getPendingTxSize|type == "number"'
        'getTotalTransactionCount|.blockNumber | type == "number"'
        'getGroupList|.groupList | index("'"${group}"'") != null'
        'getGroupPeers|type == "array" and length > 0'
        'getGroupInfo|.chainID | length > 0'
        'getGroupInfoList|type == "array" and length > 0'
    )
    local entry method filter result
    for entry in "${methods[@]}"; do
        method="${entry%%|*}"
        filter="${entry#*|}"
        if [[ "${method}" == "getGroupPeers" || "${method}" == "getGroupInfo" ]]; then
            result=$(ops "${node_dir}" attach "${method}" "${group}") || return 1
        else
            result=$(ops "${node_dir}" attach "${method}") || return 1
        fi
        assert_jq "${result}" "${filter}" "attach ${method}" || return 1
    done
    echo "scenario 1 ok: status five checks + 13 read methods on ${chain}"
}
