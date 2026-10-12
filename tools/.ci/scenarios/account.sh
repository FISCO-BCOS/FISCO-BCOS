#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 19 account: `account new` (SM on the SM chain) writes a key pair the
#             node accepts: `tx smoke --account` succeeds and the recorded `from` equals the
#             printed address. New; replaces console's get_account.sh / get_gm_account.sh.
# @ file    : account.sh

scenario_19_account()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 19 account "console get_account.sh (new scenario)"
    local -a sm=()
    [[ "${chain}" == "sm" ]] && sm=(--sm)
    local out_dir="${node_dir}/../../scenario19-accounts"
    local created address pem
    created=$("${fisco_bcos_path}" account new "${sm[@]}" --out "${out_dir}" --json) || { echo "${created}"; return 1; }
    address=$(jq_get "${created}" '.address')
    pem=$(jq_get "${created}" '.privateKeyFile')
    [[ "${address}" =~ ^0x[0-9a-fA-F]{40}$ ]] || { scenario_fail "account new address: ${address}"; return 1; }
    [[ -f "${pem}" && -f "$(jq_get "${created}" '.publicKeyFile')" ]] || { scenario_fail "account files missing"; return 1; }
    local smoke
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000 --account "${pem}") || { echo "${smoke}"; return 1; }
    assert_jq "${smoke}" '.ok == true' "tx smoke with the new account" || return 1
    assert_jq "${smoke}" '.account | ascii_downcase == "'"${address,,}"'"' "smoke signed by the new account" || return 1
    local tx
    tx=$(ops "${node_dir}" tx get "$(jq_get "${smoke}" '.steps[1].txHash')") || return 1
    assert_jq "${tx}" '.transaction.from | ascii_downcase == "'"${address,,}"'"' "tx from equals the printed address" || return 1
    echo "scenario 19 ok: ${address} on ${chain}"
}
