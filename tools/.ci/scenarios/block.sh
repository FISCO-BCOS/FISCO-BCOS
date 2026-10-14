#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 4 block-tx: block by height and by hash agree, header-only carries no
#             transactions, --txs lists them, --proof attaches the tx/receipt proofs, tx get
#             --proof, getCode / getABI of a deployed contract. Replaces console
#             clientWithParamsTest (143-205) and java-sdk BcosSDKTest.testClient.
# @ file    : block.sh

scenario_4_block_tx()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 4 block-tx "console clientWithParamsTest:143-205, java-sdk BcosSDKTest.testClient"
    # one more transaction so a recent block has a tx
    local smoke
    smoke=$(ops "${node_dir}" tx smoke) || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke ok" || return 1
    local setHash contract
    setHash=$(jq_get "${smoke}" '.steps[1].txHash')
    contract=$(jq_get "${smoke}" '.steps[0].detail' | /usr/bin/grep -o '0x[0-9a-fA-F]\{40\}' | head -n 1)
    local tx number
    tx=$(ops "${node_dir}" tx get "${setHash}" --proof) || return 1
    number=$(jq_get "${tx}" '.receipt.blockNumber')
    assert_jq "${tx}" '.transaction.txProof | length > 0' "tx get --proof has txProof" || return 1
    assert_jq "${tx}" '.receipt.txReceiptProof | length > 0' "tx get --proof has receipt proof" || return 1

    local byNumber hash byHash
    byNumber=$(ops "${node_dir}" block get "${number}") || return 1
    assert_jq "${byNumber}" '.number == '"${number}" "block get <n> number" || return 1
    assert_jq "${byNumber}" '.transactions == null' "header-only has no transactions" || return 1
    assert_jq "${byNumber}" '.txsRoot | startswith("0x")' "header txsRoot" || return 1
    hash=$(jq_get "${byNumber}" '.hash')
    byHash=$(ops "${node_dir}" block get "${hash}" --txs) || return 1
    assert_jq "${byHash}" '.number == '"${number}" "block get <hash> number" || return 1
    assert_jq "${byHash}" '.hash == "'"${hash}"'"' "block get <hash> hash" || return 1
    assert_jq "${byHash}" '.transactions | length >= 1' "--txs lists transactions" || return 1
    assert_jq "${byHash}" '[.transactions[].hash] | index("'"${setHash}"'") != null' "set tx inside its block" || return 1
    local proofs
    proofs=$(ops "${node_dir}" block get "${number}" --proof) || return 1
    assert_jq "${proofs}" '.proofs | length >= 1' "--proof per tx" || return 1
    assert_jq "${proofs}" '.proofs[0].txProof | length > 0' "--proof txProof" || return 1
    assert_jq "${proofs}" '.proofs[0].receiptProof | length > 0' "--proof receiptProof" || return 1

    local group code abi
    group=$(chain_group_id "${node_dir}")
    code=$(ops "${node_dir}" attach getCode "[\"${group}\",\"\",\"${contract}\"]") || return 1
    assert_jq "${code}" 'startswith("0x") and length > 2' "getCode of HelloWorld" || return 1
    abi=$(ops "${node_dir}" attach getABI "[\"${group}\",\"\",\"${contract}\"]") || return 1
    assert_jq "${abi}" 'type == "string" and (. | fromjson | length > 0)' "getABI of HelloWorld" || return 1
    echo "scenario 4 ok: block ${number} ${hash} on ${chain}"
}
