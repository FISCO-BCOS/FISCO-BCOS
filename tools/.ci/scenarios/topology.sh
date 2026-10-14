#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: topology scenarios on the chain's expanded node and its leader:
#             7  consensus membership (observer → sealer → removed → observer → sealer, every
#                step read back with `consensus list`; replaces java-sdk
#                PrecompiledTest.test1ConsensusService),
#             13 view change after the leader is stopped (nonsm only: three sealers plus the
#                expanded one keep a quorum of 3; new),
#             14 a node that missed 20 transactions catches up (sync_caught_up fail → ok, lag 0;
#                new).
#             Node ids come from <node>/conf/node.nodeid; the expanded node is node<N> where
#             N = chain_node_count (ci_check_air.sh expand_node).
# @ file    : topology.sh

# node_id_of <node_dir>
node_id_of()
{
    tr -d '[:space:]' < "${1}/conf/node.nodeid"
}

# node_dir_by_id <nodes_dir> <nodeId>: the node directory whose node.nodeid matches
node_dir_by_id()
{
    local node
    for node in "${1}"/node*; do
        [[ -f "${node}/conf/node.nodeid" ]] || continue
        if [[ "$(node_id_of "${node}")" == "${2}" ]]; then
            echo "${node}"
            return 0
        fi
    done
    return 1
}

# consensus_role <node_dir> <nodeId>: sealer | observer | none (from `consensus list`)
consensus_role()
{
    local list
    list=$(ops "${1}" consensus list) || return 1
    jq -r --arg id "${2}" '[.[] | select(.nodeId == $id) | .role] | first // "none"' <<<"${list}"
}

# wait_status_ok <node_dir> <seconds>: poll `status` until it exits 0 with every check ok;
# a fresh chain stays in timeout until its first block, and until #5643 every block is followed
# by one consensus-timeout view change about one timeout later, so one failing snapshot is not a
# verdict
wait_status_ok()
{
    local node_dir="${1}"
    local limit="${2}"
    local waited=0 status
    while (( waited < limit )); do
        if status=$("${fisco_bcos_path}" status --node-dir "${node_dir}" --json 2>/dev/null) &&
            jq -e '[.checks[] | .ok] | all' >/dev/null <<<"${status}"; then
            echo "${status}"
            return 0
        fi
        sleep 2
        waited=$((waited + 2))
    done
    scenario_fail "status on ${node_dir} not ok after ${limit}s: $(head -c 600 <<<"${status}")"
    return 1
}

# consensus_write <node_dir> <expect: ok|<codeName>> <args...>: a consensus write through the
# preset table; on the auth chain the write is a committee proposal (single governor, rates
# 0/0: executed in the creating transaction) and the precompiled code sits in the proposal's
# execResult; the plain chains call the precompiled directly
consensus_write()
{
    local node_dir="${1}"
    local expect="${2}"
    shift 2
    local result
    if [[ "${expect}" == "ok" ]]; then
        result=$(ops "${node_dir}" consensus "$@" --yes --timeout 60000 "${topology_account[@]}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.ok == true' "consensus $*" || return 1
    else
        result=$(ops_expect_exit 2 "${node_dir}" consensus "$@" --yes --timeout 60000 "${topology_account[@]}") || return 1
        assert_jq "${result}" '.ok == false and ((.codeName // .proposal.execCodeName) == "'"${expect}"'")' \
            "consensus $* answers ${expect}" || return 1
    fi
    if [[ "${topology_chain}" == "auth" ]]; then
        assert_jq "${result}" '.mode == "proposal" and .proposal.executed == true' "auth chain routes consensus $* through a proposal" || return 1
    else
        assert_jq "${result}" '.mode == "direct"' "plain chain calls the consensus precompiled directly" || return 1
    fi
    echo "${result}"
}

topology_setup()
{
    topology_chain="${1}"
    topology_account=()
    if [[ "${topology_chain}" == "auth" ]]; then
        local admin
        admin=$(chain_admin_pem "${2}")
        [[ -f "${admin}" ]] || { scenario_fail "auth admin pem not found next to ${2}"; return 1; }
        topology_account=(--account "${admin}")
    fi
}

scenario_7_consensus_membership()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 7 consensus-membership "java-sdk PrecompiledTest.test1ConsensusService"
    topology_setup "${chain}" "${node_dir}" || return 1
    local new_node new_id role
    new_node=$(chain_node_dir "${chain}" "$(chain_node_count "${chain}")")
    [[ -f "${new_node}/conf/node.nodeid" ]] || { scenario_fail "expanded node ${new_node} missing"; return 1; }
    new_id=$(node_id_of "${new_node}")
    role=$(consensus_role "${node_dir}" "${new_id}") || return 1
    [[ "${role}" == "none" ]] || { scenario_fail "expanded node already in the consensus list as ${role}"; return 1; }
    # a node absent from the list cannot become a sealer in one step (ConsensusPrecompiled.cpp:
    # CODE_ADD_SEALER_SHOULD_IN_OBSERVER), and a malformed id is refused before any lookup
    consensus_write "${node_dir}" SEALER_MUST_BE_OBSERVER_FIRST add-sealer "${new_id}" 1 >/dev/null || return 1
    consensus_write "${node_dir}" INVALID_NODE_ID add-sealer deadbeef 1 >/dev/null || return 1
    consensus_write "${node_dir}" ok add-observer "${new_id}" >/dev/null || return 1
    role=$(consensus_role "${node_dir}" "${new_id}") || return 1
    [[ "${role}" == "observer" ]] || { scenario_fail "after add-observer the role is ${role}"; return 1; }
    consensus_write "${node_dir}" ok add-sealer "${new_id}" 1 >/dev/null || return 1
    local list
    list=$(ops "${node_dir}" consensus list) || return 1
    assert_jq "${list}" '[.[] | select(.nodeId == "'"${new_id}"'")] | length == 1 and .[0].role == "sealer" and .[0].weight == 1' \
        "after add-sealer the node is a sealer of weight 1" || return 1
    # the chain still produces blocks with the new sealer in the set. On a 2-node chain the
    # third sealer makes the quorum 3 of 3, so blocks wait until the new node seals (a view
    # change away); poll status first and give the smoke a longer request timeout
    local smoke
    wait_status_ok "${node_dir}" 90 >/dev/null || return 1
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000) || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke with the new sealer" || return 1
    # add-sealer on a node that already is a sealer updates its weight in place (code 0,
    # ConsensusPrecompiled.cpp addSealer), it is not an error
    consensus_write "${node_dir}" ok add-sealer "${new_id}" 2 >/dev/null || return 1
    list=$(ops "${node_dir}" consensus list) || return 1
    assert_jq "${list}" '[.[] | select(.nodeId == "'"${new_id}"'")][0].weight == 2' "add-sealer on a sealer updates the weight" || return 1
    consensus_write "${node_dir}" ok remove "${new_id}" >/dev/null || return 1
    role=$(consensus_role "${node_dir}" "${new_id}") || return 1
    [[ "${role}" == "none" ]] || { scenario_fail "after remove the role is ${role}"; return 1; }
    consensus_write "${node_dir}" ok add-observer "${new_id}" >/dev/null || return 1
    consensus_write "${node_dir}" ok add-sealer "${new_id}" 1 >/dev/null || return 1
    role=$(consensus_role "${node_dir}" "${new_id}") || return 1
    [[ "${role}" == "sealer" ]] || { scenario_fail "after the second add-sealer the role is ${role}"; return 1; }
    wait_status_ok "${node_dir}" 90 >/dev/null || return 1
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000) || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke after the membership round trip" || return 1
    echo "scenario 7 ok: $(basename "${new_node}") observer → sealer → removed → observer → sealer on ${chain}"
}

scenario_13_viewchange()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 13 viewchange "new scenario: kill the leader, the survivors elect another"
    local nodes_dir
    nodes_dir=$(dirname "${node_dir}")
    local sealers
    sealers=$(ops "${node_dir}" consensus list) || return 1
    sealers=$(jq '[.[] | select(.role == "sealer")] | length' <<<"${sealers}")
    if [[ "${chain}" != "nonsm" ]] || (( sealers < 4 )); then
        echo "scenario 13 skip: needs 3 sealers plus the expanded one to keep a quorum while one is down (${chain}: ${sealers} sealers)"
        return 0
    fi
    local status
    status=$(wait_status_ok "${node_dir}" 60) || return 1
    local cs leader_index leader_id leader_dir survivor view_before height_before
    cs=$(ops "${node_dir}" attach getConsensusStatus) || return 1
    leader_index=$(jq_get "${cs}" '.leaderIndex')
    leader_id=$(jq_get "${cs}" '.consensusNodeList[] | select(.index == '"${leader_index}"') | .nodeID')
    view_before=$(jq_get "${cs}" '.view')
    height_before=$(jq_get "${status}" '.chain.blockNumber')
    leader_dir=$(node_dir_by_id "${nodes_dir}" "${leader_id}") || { scenario_fail "leader ${leader_id} has no local node dir"; return 1; }
    survivor="${node_dir}"
    [[ "${leader_dir}" != "${node_dir}" ]] || survivor=$(chain_node_dir "${chain}" 1)
    echo "scenario 13: leader index ${leader_index} = $(basename "${leader_dir}") at view ${view_before}, height ${height_before}; survivor $(basename "${survivor}")"
    bash "${leader_dir}/stop.sh" >/dev/null 2>&1
    local stopped=${SECONDS}
    # the survivor sees the timeout (consensus_stable fail, exit 2) and then a new leader
    local waited=0 saw_timeout="" new_leader_at="" new_index probe
    while (( waited < 90 )); do
        if probe=$("${fisco_bcos_path}" status --node-dir "${survivor}" --json 2>/dev/null); then
            :
        elif (( $? == 2 )) && [[ -z "${saw_timeout}" ]] &&
            jq -e '[.checks[] | select(.name == "consensus_stable") | .state] == ["fail"]' >/dev/null <<<"${probe}"; then
            saw_timeout=$((SECONDS - stopped))
            echo "scenario 13: consensus_stable fail seen ${saw_timeout}s after the leader stopped"
        fi
        new_index=$(jq -r '.consensus.leaderIndex // empty' <<<"${probe}")
        if [[ -n "${new_index}" && "${new_index}" != "${leader_index}" ]] &&
            jq -e '.consensus.inTimeout == false' >/dev/null <<<"${probe}"; then
            new_leader_at=$((SECONDS - stopped))
            echo "scenario 13: leader index ${new_index} ${new_leader_at}s after the leader stopped"
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done
    [[ -n "${new_leader_at}" ]] || { scenario_fail "no new leader within ${waited}s of stopping $(basename "${leader_dir}")"; return 1; }
    [[ -n "${saw_timeout}" ]] || echo "scenario 13: the consensus_stable fail window was shorter than one status poll (not asserted)"
    # the survivor's own log shows the round: ViewChangeTriggered reason=consensus_timeout to
    # a view beyond the one before the stop, reaching a leader
    local trace
    trace=$(ops "${survivor}" log viewchange --since 10m --last 50) || return 1
    # PBFTEngine.cpp:1617 emits ViewChangeTriggered for consensus_timeout and for
    # f_plus_one_higher_view (a follower joins a view change it saw from f+1 peers); the row
    # must name a leader other than the stopped one, which idle-chain timeout flapping (a view
    # change that lands back on the same leader) cannot satisfy
    assert_jq "${trace}" '[.rows[] | select((.reason == "consensus_timeout" or .reason == "f_plus_one_higher_view") and (.toView | tonumber) > '"${view_before}"' and .newLeader != "-" and .newLeader != "" and (.newLeader | tostring) != "'"${leader_index}"'")] | length > 0' \
        "log viewchange has a timeout/f+1 round after view ${view_before} naming a leader other than index ${leader_index}" || return 1
    # 3 of 4 sealers keep producing
    local smoke
    smoke=$(ops "${survivor}" tx smoke --timeout 60000) || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke with the leader down" || return 1
    # the stopped node already held height_before; it must catch up to what the survivors
    # produced while it was down
    local height_after
    height_after=$(ops "${survivor}" attach getBlockNumber) || return 1
    [[ "${height_after}" =~ ^[0-9]+$ ]] && (( height_after > height_before )) ||
        { scenario_fail "survivor height ${height_after} did not advance past ${height_before} with the leader down"; return 1; }
    bash "${leader_dir}/start.sh" >/dev/null 2>&1
    local restarted=${SECONDS}
    status=$(wait_status_ok "${leader_dir}" 120) || return 1
    assert_jq "${status}" '.chain.blockNumber >= '"${height_after}" "restarted leader reached the survivors' height ${height_after}" || return 1
    echo "scenario 13 ok on ${chain}: timeout seen ${saw_timeout:--}s / new leader ${new_leader_at}s after stop; $(basename "${leader_dir}") back with all checks ok $((SECONDS - restarted))s after restart"
}

scenario_14_sync()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 14 sync "new scenario: a node that missed 20 transactions catches up"
    topology_setup "${chain}" "${node_dir}" || return 1
    local new_node new_id
    new_node=$(chain_node_dir "${chain}" "$(chain_node_count "${chain}")")
    new_id=$(node_id_of "${new_node}")
    # scenario 7 left the expanded node a sealer; as an observer it can be stopped and wiped
    # without touching the quorum (2-node chains would halt with a third sealer down)
    consensus_write "${node_dir}" ok add-observer "${new_id}" >/dev/null || return 1
    bash "${new_node}/stop.sh" >/dev/null 2>&1
    local pids
    pids=$(pgrep -f "${new_node}/" || true)
    [[ -z "${pids}" ]] || kill -9 ${pids} 2>/dev/null || true
    rm -rf "${new_node}/data" "${new_node}/log"
    # 20 transactions the node will have to fetch: one deploy plus 19 sets
    local before
    before=$(ops "${node_dir}" attach getBlockNumber) || return 1
    [[ "${before}" =~ ^[0-9]+$ ]] || { scenario_fail "getBlockNumber before the backlog: ${before}"; return 1; }
    local abi deploy deployed sent i
    abi=$(contract_abi HelloWorld) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin HelloWorld "${chain}")" --abi "${abi}" --timeout 60000) || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0' "deploy HelloWorld for the sync backlog" || return 1
    deployed=$(jq_get "${deploy}" '.contractAddress')
    for i in $(seq 1 19); do
        sent=$(ops "${node_dir}" tx send "${deployed}" --abi "${abi}" set "sync ${i}" --timeout 60000) || { echo "${sent}"; return 1; }
        assert_jq "${sent}" '.status == 0' "tx send set ${i}" || return 1
    done
    # the backlog is 20 blocks (every tx seals its own block on an idle chain); a status
    # time-out must not turn into target=null, which jq compares as smaller than any number
    local target
    target=$(ops "${node_dir}" attach getBlockNumber) || return 1
    [[ "${target}" =~ ^[0-9]+$ ]] && (( target >= before + 20 )) ||
        { scenario_fail "height after 20 transactions is ${target}, expected >= $((before + 20))"; return 1; }
    bash "${new_node}/start.sh" >/dev/null 2>&1
    local started=${SECONDS} waited=0 probe first_state="" lag_seen="" caught=""
    while (( waited < 180 )); do
        if probe=$("${fisco_bcos_path}" status --node-dir "${new_node}" --json 2>/dev/null); then
            :
        elif (( $? != 2 )); then
            # the socket is not up yet
            sleep 1
            waited=$((waited + 1))
            continue
        fi
        if [[ -z "${first_state}" ]]; then
            first_state=$(jq -r '[.checks[] | select(.name == "sync_caught_up") | .state][0] // "absent"' <<<"${probe}")
            echo "scenario 14: first status $((SECONDS - started))s after start: sync_caught_up ${first_state}, height $(jq -r '.chain.blockNumber' <<<"${probe}") of ${target}"
        fi
        if jq -e '.sync.lag | type == "number"' >/dev/null <<<"${probe}"; then
            lag_seen=1
        fi
        if jq -e '([.checks[] | select(.name == "sync_caught_up") | .state] == ["ok"]) and .sync.lag == 0 and .chain.blockNumber >= '"${target}" >/dev/null <<<"${probe}"; then
            caught=$((SECONDS - started))
            break
        fi
        sleep 1
        waited=$((waited + 1))
    done
    [[ -n "${caught}" ]] || { scenario_fail "$(basename "${new_node}") did not reach height ${target} with lag 0 within ${waited}s: $(head -c 600 <<<"${probe}")"; return 1; }
    [[ -n "${lag_seen}" ]] || { scenario_fail "status never reported a numeric sync.lag"; return 1; }
    case "${first_state}" in
        fail) ;;
        ok | skipped)
            # it answered already at the target height: nothing to watch, the final state holds
            jq -e '.chain.blockNumber >= '"${target}" >/dev/null <<<"${probe}" ||
                { scenario_fail "first probe said ${first_state} below the target height ${target}"; return 1; }
            echo "scenario 14: the node caught up before the first status probe (${first_state} at height >= ${target}); only the final state is asserted" ;;
        *) scenario_fail "unexpected first sync_caught_up state ${first_state}"; return 1 ;;
    esac
    echo "scenario 14 ok on ${chain}: $(basename "${new_node}") re-synced ${target} blocks (first probe ${first_state}) in ${caught}s"
}
