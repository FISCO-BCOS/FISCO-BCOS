#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 18 auth-governance (auth chain only): the committee of the -a admin,
#             deploy admission switched to the whitelist through an automatic proposal, a new
#             account refused and then admitted, a consensus change through a proposal, a
#             second governor, the vote rates raised so a proposal waits for both votes, and
#             the proposal list. Ends with the rates back at 0/0 and deploy admission open
#             again so the scenarios after it keep auto-executing and deploying. New (CI never ran an auth chain before).
# @ file    : governance.sh

# gov_write <node_dir> <pem> <args...>: a write through the preset table that must go out as
# a proposal; prints the JSON
gov_write()
{
    local node_dir="${1}"
    local pem="${2}"
    shift 2
    local result
    result=$(ops "${node_dir}" "$@" --yes --timeout 60000 --account "${pem}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.mode == "proposal"' "$* goes through a proposal" || return 1
    echo "${result}"
}

# gov_proposal_status <node_dir> <id>: statusName of a proposal
gov_proposal_status()
{
    ops "${1}" committee proposal "${2}" | jq -r '.statusName'
}

scenario_18_auth_governance()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 18 auth-governance "new: auth chain governance"
    if [[ "${chain}" != "auth" ]]; then
        echo "scenario 18 skip: ${chain} is not an auth chain"
        return 0
    fi
    local admin
    admin=$(chain_admin_pem "${node_dir}")
    [[ -f "${admin}" ]] || { scenario_fail "auth admin pem not found next to ${node_dir}"; return 1; }
    local admin_addr
    admin_addr=$(pem_address "${admin}")
    local info
    info=$(ops "${node_dir}" committee info) || return 1
    assert_jq "${info}" '(.governors | length) == 1 and (.governors[0].address | ascii_downcase) == "'"${admin_addr,,}"'"' \
        "committee has the -a admin as its single governor" || return 1
    assert_jq "${info}" '(.participatesRate | tostring) == "0" and (.winRate | tostring) == "0"' "rates start at 0/0" || return 1

    # deploy admission: whitelist (type 1) through an automatic proposal; one governor with
    # rates 0/0 passes and executes it in the creating transaction
    local result
    result=$(gov_write "${node_dir}" "${admin}" auth set-deploy-auth-type 1) || return 1
    assert_jq "${result}" '.ok == true and .proposal.statusName == "passed" and .proposal.executed == true' \
        "set-deploy-auth-type proposal executed" || return 1
    local deploy_type
    deploy_type=$(ops "${node_dir}" auth deploy-type) || return 1
    assert_jq "${deploy_type}" '(.decoded | tostring) | test("1")' "deploy-type reads 1 (whitelist)" || return 1

    # a fresh account cannot deploy, then is admitted
    local acct_dir="${node_dir}/../../scenario18-accounts"
    mkdir -p "${acct_dir}"
    local acct acct_addr
    acct=$("${fisco_bcos_path}" account new --out "${acct_dir}" --json | jq -r '.privateKeyFile') || return 1
    [[ -f "${acct}" ]] || { scenario_fail "account new wrote no pem: ${acct}"; return 1; }
    acct_addr=$(pem_address "${acct}")
    local smoke
    smoke=$(ops_expect_exit 2 "${node_dir}" tx smoke --timeout 60000 --account "${acct}") || return 1
    assert_jq "${smoke}" '.ok == false and .reason == "auth_denied"' "new account's deploy is refused (auth_denied)" || return 1
    local has_auth
    has_auth=$(ops "${node_dir}" auth has-deploy-auth "${acct_addr}") || return 1
    assert_jq "${has_auth}" '(.decoded | tostring) | test("false")' "has-deploy-auth false before open" || return 1
    result=$(gov_write "${node_dir}" "${admin}" auth open-deploy-auth "${acct_addr}") || return 1
    assert_jq "${result}" '.ok == true and .proposal.executed == true' "open-deploy-auth proposal executed" || return 1
    has_auth=$(ops "${node_dir}" auth has-deploy-auth "${acct_addr}") || return 1
    assert_jq "${has_auth}" '(.decoded | tostring) | test("true")' "has-deploy-auth true after open" || return 1
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000 --account "${acct}") || return 1
    assert_jq "${smoke}" '.ok == true' "admitted account deploys HelloWorld" || return 1
    local contract
    contract=$(jq_get "${smoke}" '.steps[0].detail' | /usr/bin/grep -o '0x[0-9a-fA-F]\{40\}' | head -n 1)
    [[ -n "${contract}" ]] || { scenario_fail "smoke detail carries no contract address: ${smoke}"; return 1; }
    local admin_of
    admin_of=$(ops "${node_dir}" auth get-admin "${contract}") || return 1
    assert_jq "${admin_of}" '(.decoded | tostring | ascii_downcase) | test("'"${acct_addr,,}"'")' "get-admin is the deployer" || return 1

    # a consensus change goes out as a proposal: the expanded node is an observer after
    # scenario 14, promote it
    local nodes_dir
    nodes_dir=$(cd "${node_dir}/.." && pwd)
    local new_node new_id
    new_node="${nodes_dir}/node$(( $(ls -d "${nodes_dir}"/node* | wc -l) - 1 ))"
    new_id=$(cat "${new_node}/conf/node.nodeid")
    local role
    role=$(ops "${node_dir}" consensus list | jq -r --arg id "${new_id}" '[.[] | select(.nodeId == $id) | .role] | first // "none"')
    if [[ "${role}" != "sealer" ]]; then
        result=$(gov_write "${node_dir}" "${admin}" consensus add-sealer "${new_id}" 1) || return 1
        assert_jq "${result}" '.ok == true and .proposal.executed == true' "add-sealer proposal executed" || return 1
    fi
    role=$(ops "${node_dir}" consensus list | jq -r --arg id "${new_id}" '[.[] | select(.nodeId == $id) | .role] | first // "none"')
    [[ "${role}" == "sealer" ]] || { scenario_fail "after the add-sealer proposal the role is ${role}"; return 1; }

    # a second governor, then rates that need both votes
    local acct2 acct2_addr
    acct2=$("${fisco_bcos_path}" account new --out "${acct_dir}" --json | jq -r '.privateKeyFile') || return 1
    acct2_addr=$(pem_address "${acct2}")
    result=$(ops "${node_dir}" committee propose-governor "${acct2_addr}" 1 --yes --timeout 60000 --account "${admin}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.ok == true and .proposal.executed == true' "propose-governor executed by the single governor" || return 1
    info=$(ops "${node_dir}" committee info) || return 1
    assert_jq "${info}" '(.governors | length) == 2' "committee has two governors" || return 1
    result=$(ops "${node_dir}" committee propose-rate 100 100 --yes --timeout 60000 --account "${admin}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.ok == true and .proposal.executed == true' "propose-rate 100/100 executed (rates were 0/0)" || return 1
    info=$(ops "${node_dir}" committee info) || return 1
    assert_jq "${info}" '(.participatesRate | tostring) == "100" and (.winRate | tostring) == "100"' "rates are 100/100" || return 1

    # now a proposal waits for the second vote
    result=$(ops "${node_dir}" committee propose-sysconfig tx_count_limit 2500 --yes --timeout 60000 --account "${admin}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.proposal.statusName == "voting" and .proposal.executed == false' "propose-sysconfig waits for the second governor" || return 1
    local proposal_id
    proposal_id=$(jq_get "${result}" '.proposal.id')
    [[ "${proposal_id}" =~ ^[0-9]+$ ]] || { scenario_fail "no proposal id in ${result}"; return 1; }
    [[ "$(gov_proposal_status "${node_dir}" "${proposal_id}")" == "voting" ]] || { scenario_fail "proposal ${proposal_id} is not voting"; return 1; }
    result=$(ops "${node_dir}" committee vote "${proposal_id}" true --yes --timeout 60000 --account "${acct2}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.proposal.statusName == "passed" and .proposal.executed == true' "the second vote passes and executes the proposal" || return 1
    local readback
    readback=$(ops "${node_dir}" sysconfig get tx_count_limit) || return 1
    assert_jq "${readback}" '.value == "2500"' "tx_count_limit set by the voted proposal" || return 1
    local list
    list=$(ops "${node_dir}" committee proposals --last 5) || return 1
    assert_jq "${list}" '(.proposals | length) >= 3 and ([.proposals[] | select(.id == "'"${proposal_id}"'") | .statusName] == ["passed"])' \
        "proposals --last 5 lists the voted proposal as passed" || return 1

    # back to 0/0 so the scenarios after this one (features) auto-execute again; both vote
    result=$(ops "${node_dir}" committee propose-rate 0 0 --yes --timeout 60000 --account "${admin}") || { echo "${result}"; return 1; }
    proposal_id=$(jq_get "${result}" '.proposal.id')
    if [[ "$(jq_get "${result}" '.proposal.executed')" != "true" ]]; then
        result=$(ops "${node_dir}" committee vote "${proposal_id}" true --yes --timeout 60000 --account "${acct2}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.proposal.executed == true' "propose-rate 0/0 executed after the second vote" || return 1
    fi
    info=$(ops "${node_dir}" committee info) || return 1
    assert_jq "${info}" '(.participatesRate | tostring) == "0" and (.winRate | tostring) == "0"' "rates restored to 0/0" || return 1
    # and deploy admission back to open (type 0): the whitelist admits only accounts opened
    # explicitly, not even the -a admin, and scenario 17's closing smoke deploys
    result=$(gov_write "${node_dir}" "${admin}" auth set-deploy-auth-type 0) || return 1
    assert_jq "${result}" '.ok == true and .proposal.executed == true' "set-deploy-auth-type 0 proposal executed" || return 1
    echo "scenario 18 ok: deploy whitelist, admission, consensus proposal, second governor, two-vote proposal ${proposal_id} on ${chain}"
}
