#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 2 hello: deploy HelloWorld, set, get through `tx smoke`, then read the
#             two transactions back and check the receipts. Replaces console
#             ConsoleClientTest.clientWithParamsTest (96-117), java-sdk
#             BcosSDKTest.testHelloWorldInSolidity and the AssembleTransactionProcessorTest
#             HelloWorld cases. Deploy/send/call with an external ABI file arrive with the
#             vendored contracts (scenario 9/10).
# @ file    : hello.sh

scenario_2_hello()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 2 hello "console clientWithParamsTest:96-117, java-sdk BcosSDKTest.testHelloWorldInSolidity, AssembleTransactionProcessorTest HelloWorld"
    local smoke
    smoke=$(ops "${node_dir}" tx smoke) || { echo "${smoke}"; return 1; }
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
    echo "scenario 2 ok: HelloWorld at ${contract} on ${chain}"
}
