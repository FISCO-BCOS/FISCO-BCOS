#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 9 contract-test-all: deploy the vendored ContractTestAll (the SM
#             chain deploys the ECRecoverSMTest variant), fund it, run check() — which
#             deploys and exercises CodeOp / Create2 / DelegateCall / EventTest / LibraryTest /
#             ProxyTest / TablePrecompiledTest / ECRecoverTest / DeployTreeTest / Balance* /
#             StaticCall — then repeat after feature_dmc2serial is on. Asserts the receipt
#             status, the event count and the block's gasUsed. Replaces java-sdk-demo
#             java_sdk_demo_ci_test.sh check_all_contract (179-213) and the two contract_test
#             rounds (227-254). Needs scenario 8 (balance feature on, governor is a caller).
# @ file    : contracts.sh

# contract_test_all_round <chain> <node_dir> <governor pem> <label>
contract_test_all_round()
{
    local chain="${1}"
    local node_dir="${2}"
    local governor="${3}"
    local label="${4}"
    local abi
    abi=$(contract_abi ContractTestAll) || return 1
    # check() deploys fourteen contracts and burns ~7e8 gas; with auth checks on every call
    # and four chains sharing a debug-build runner it outlives the default 15 s request timeout
    local -a slow=(--timeout 180000)
    local deploy address
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin ContractTestAll "${chain}")" --abi "${abi}" --account "${governor}" "${slow[@]}") || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0' "deploy ContractTestAll (${label})" || return 1
    address=$(jq_get "${deploy}" '.contractAddress')
    local block
    block=$(ops "${node_dir}" block get "$(jq_get "${deploy}" '.blockNumber')") || return 1
    assert_jq "${block}" '(.gasUsed | tonumber) > 0' "deploy block gasUsed > 0 (${label})" || return 1
    local fund
    fund=$(ops "${node_dir}" balance add "${address}" 10000000 --yes --account "${governor}") || { echo "${fund}"; return 1; }
    assert_jq "${fund}" '.ok == true' "fund ContractTestAll (${label})" || return 1
    local check
    check=$(ops "${node_dir}" tx send "${address}" --abi "${abi}" check --account "${governor}" "${slow[@]}") || { echo "${check}"; return 1; }
    assert_jq "${check}" '.status == 0' "ContractTestAll.check() status 0 (${label})" || return 1
    assert_jq "${check}" '.logEntries | length > 0' "check() emitted Info events (${label})" || return 1
    block=$(ops "${node_dir}" block get "$(jq_get "${check}" '.blockNumber')") || return 1
    assert_jq "${block}" '(.gasUsed | tonumber) > 0' "check block gasUsed > 0 (${label})" || return 1
    echo "scenario 9 (${label}): ContractTestAll at ${address}, check() gasUsed $(jq_get "${check}" '.gasUsed'), $(jq_get "${check}" '.logEntries | length') events"
}

scenario_9_contract_test_all()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 9 contract-test-all "java-sdk-demo check_all_contract:179-213, contract_test:227-254"
    local governor
    governor=$(chain_governor_pem "${node_dir}")
    [[ -f "${governor}" ]] || { scenario_fail "no governor pem for ${node_dir}"; return 1; }
    contract_test_all_round "${chain}" "${node_dir}" "${governor}" "default" || return 1
    local serial
    serial=$(ops "${node_dir}" sysconfig set feature_dmc2serial 1 --yes --account "${governor}") || { echo "${serial}"; return 1; }
    assert_jq "${serial}" '.ok == true' "enable feature_dmc2serial" || return 1
    local features
    features=$(ops "${node_dir}" sysconfig features) || return 1
    assert_jq "${features}" 'map(select(.name == "feature_dmc2serial") | .enabled) == [true]' "feature_dmc2serial on" || return 1
    contract_test_all_round "${chain}" "${node_dir}" "${governor}" "dmc2serial" || return 1
    echo "scenario 9 ok: ContractTestAll check() passed before and after feature_dmc2serial on ${chain}"
}
