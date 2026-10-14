#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 20 ops-status: the built-in operator commands run beside the chain —
#             status, tx smoke, log status (the node's own log files agree with the RPC
#             snapshot on height, and keep identifying the node while a peer is down),
#             tui --version-check, and tui --snapshot-dir rendering every page. Existing
#             ops_tool_test.
# @ file    : ops.sh

scenario_20_ops_status()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 20 ops-status "existing ops_tool_test"
    local status smoke logStatus rc
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000) || return 1
    assert_jq "${smoke}" '.ok == true' "tx smoke ok" || return 1
    status=$(ops "${node_dir}" status) && rc=0 || rc=$?
    assert_checks_but_view_change "${status}" "${rc}" "status: every check ok but a view-change consensus_stable" "ok" || return 1
    # the log snapshot leaves the checks it cannot judge from the log files "skipped"
    logStatus=$(ops "${node_dir}" log status) && rc=0 || rc=$?
    assert_jq "${logStatus}" '.source == "log"' "log status reads the log files" || return 1
    assert_checks_but_view_change "${logStatus}" "${rc}" \
        "log status: no failing check other than consensus_stable" "ok skipped" || return 1
    assert_jq "${logStatus}" '.chain.blockNumber | type == "number"' "log status has a height" || return 1
    local rpcHeight logHeight
    rpcHeight=$(jq_get "${status}" '.chain.blockNumber')
    logHeight=$(jq_get "${logStatus}" '.chain.blockNumber')
    # the log is written when the block is committed; it may trail the RPC by the block in flight
    if (( rpcHeight - logHeight > 2 || logHeight > rpcHeight )); then
        scenario_fail "log status height ${logHeight} vs rpc ${rpcHeight}"
        return 1
    fi
    "${fisco_bcos_path}" tui --version-check || { scenario_fail "tui --version-check"; return 1; }
    ops_log_status_across_stopped_peer "${node_dir}" "${logStatus}" || return 1
    ops_tui_snapshots "${node_dir}" || return 1
    echo "scenario 20 ok: status/smoke/log status (peer down and back)/tui --version-check/snapshots on ${chain}"
}

# log status keeps answering from node0's own log files while a peer is down: the identity it
# reads stays the same and the height does not go backwards. node3 is an observer by now
# (scenario 14 demoted it), so stopping it leaves consensus alone.
ops_log_status_across_stopped_peer()
{
    local node_dir="${1}"
    local before="${2}"
    local peer
    peer="$(dirname "${node_dir}")/node3"
    [[ -f "${peer}/stop.sh" ]] || peer="$(dirname "${node_dir}")/node1"
    bash "${peer}/stop.sh" >/dev/null 2>&1 || { scenario_fail "stop ${peer}"; return 1; }
    local after
    after=$(ops "${node_dir}" log status) || { (( $? == 2 )) || { bash "${peer}/start.sh" >/dev/null 2>&1; return 1; }; }
    bash "${peer}/start.sh" >/dev/null 2>&1 || { scenario_fail "restart ${peer}"; return 1; }
    local field
    for field in nodeID version chainId groupId smCrypto authCheck mode; do
        if [[ "$(jq_get "${before}" ".identity.${field}")" != "$(jq_get "${after}" ".identity.${field}")" ]]; then
            scenario_fail "log status identity.${field} changed while $(basename "${peer}") was down"
            return 1
        fi
    done
    assert_jq "${after}" ".chain.blockNumber >= $(jq_get "${before}" '.chain.blockNumber')" \
        "log status height does not go backwards with a peer down" || return 1
    # node3 is an observer: quorum_connected is "skipped" (not a consensus node), so wait for
    # no failing check and sync caught up rather than every check ok
    local waited=0 probe=""
    while (( waited < 120 )); do
        probe=$("${fisco_bcos_path}" status --node-dir "${peer}" --json 2>/dev/null) &&
            jq -e '([.checks[] | select(.state == "fail")] | length == 0)
                and ([.checks[] | select(.name == "sync_caught_up") | .state] == ["ok"])' >/dev/null <<<"${probe}" &&
            return 0
        sleep 2
        waited=$((waited + 2))
    done
    scenario_fail "$(basename "${peer}") not back after 120s: $(jq -c '[.checks[] | {name,state,detail}]' <<<"${probe}" 2>&1 | head -c 600)"
    return 1
}

# every page the TUI can show, rendered headlessly from live data; a non-auth chain's gov and
# proposal pages say so instead of failing
ops_tui_snapshots()
{
    local node_dir="${1}"
    local snap
    snap=$(mktemp -d "${TMPDIR:-/tmp}/ci-snap.XXXXXX")
    "${fisco_bcos_path}" tui --snapshot-dir "${snap}" --node-dir "${node_dir}" >/dev/null 2>&1 ||
        { scenario_fail "tui --snapshot-dir"; rm -rf "${snap}"; return 1; }
    local page n=1 missing=()
    for page in home home-wide consensus node log help block tx send send-confirm gov proposal; do
        [[ -s "${snap}/${n}-${page}.txt" ]] || missing+=("${n}-${page}")
        n=$((n + 1))
    done
    if (( ${#missing[@]} )); then
        scenario_fail "tui snapshots missing or empty: ${missing[*]} (have: $(ls "${snap}" | tr '\n' ' '))"
        rm -rf "${snap}"
        return 1
    fi
    if ! /usr/bin/grep -q "not an auth chain" "${snap}/11-gov.txt"; then
        scenario_fail "gov snapshot on a non-auth chain does not say so"
        rm -rf "${snap}"
        return 1
    fi
    echo "scenario 20: tui --snapshot-dir rendered $((n - 1)) pages"
    rm -rf "${snap}"
}
