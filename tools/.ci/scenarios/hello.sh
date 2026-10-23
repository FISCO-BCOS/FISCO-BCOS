#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 2 hello: deploy HelloWorld, set, get through `tx smoke`, then read the
#             two transactions back and check the receipts. Replaces console
#             ConsoleClientTest.clientWithParamsTest (96-117), java-sdk
#             BcosSDKTest.testHelloWorldInSolidity and the AssembleTransactionProcessorTest
#             HelloWorld cases, then deploys the vendored HelloWorld (tools/.ci/contracts)
#             with its ABI file: `tx deploy` / `tx send set` / `tx call get` (console deploy /
#             call with a .sol).
# @ file    : hello.sh

scenario_2_hello()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 2 hello "console clientWithParamsTest:96-117, java-sdk BcosSDKTest.testHelloWorldInSolidity, AssembleTransactionProcessorTest HelloWorld"
    local smoke
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000) || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke ok" || return 1
    assert_jq "${smoke}" '.steps | length == 3' "three smoke steps" || return 1
    assert_jq "${smoke}" '.steps[2].detail | test("Hello, FISCO BCOS")' "get returns the set value" || return 1
    local contract
    contract=$(jq_get "${smoke}" '.steps[0].detail' | /usr/bin/grep -o '0x[0-9a-fA-F]\{40\}' | head -n 1)
    [[ -n "${contract}" ]] || { scenario_fail "no contract address in deploy step"; return 1; }
    local hash tx
    for hash in $(jq_get "${smoke}" '.steps[0].txHash, .steps[1].txHash'); do
        tx=$(ops "${node_dir}" tx get "${hash}") || return 1
        assert_jq "${tx}" '.receipt.status == 0' "receipt ${hash} status 0" || return 1
        assert_jq "${tx}" '.receipt.blockNumber > 0' "receipt ${hash} in a block" || return 1
        assert_jq "${tx}" '.transaction.hash == "'"${hash}"'"' "transaction hash round trip" || return 1
    done
    local setTx
    setTx=$(ops "${node_dir}" tx get "$(jq_get "${smoke}" '.steps[1].txHash')") || return 1
    assert_jq "${setTx}" '.transaction.to | ascii_downcase == "'"${contract,,}"'"' "set goes to the deployed contract" || return 1
    # the external-ABI path: deploy from the vendored bin/abi, send set, call get
    local abi deploy deployed sent got
    abi=$(contract_abi HelloWorld) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin HelloWorld "${chain}")" --abi "${abi}") || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0 and (.contractAddress | test("^0x[0-9a-fA-F]{40}$"))' "tx deploy HelloWorld from the vendored bin" || return 1
    deployed=$(jq_get "${deploy}" '.contractAddress')
    sent=$(ops "${node_dir}" tx send "${deployed}" --abi "${abi}" set "native ci ${chain}") || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0' "tx send set" || return 1
    got=$(ops "${node_dir}" tx call "${deployed}" --abi "${abi}" get) || { echo "${got}"; return 1; }
    assert_jq "${got}" '.status == 0 and (.decoded | tostring | test("native ci '"${chain}"'"))' "tx call get decodes the set value" || return 1
    echo "scenario 2 ok: HelloWorld at ${contract} (smoke) and ${deployed} (vendored abi) on ${chain}"
}
