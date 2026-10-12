#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 15 admission: the four ways the node refuses a transaction and the
#             code each one carries. The txpool refusals come back as a JSON-RPC error from
#             sendTransaction with the protocol::TransactionStatus value
#             (bcos-protocol/bcos-protocol/TransactionStatus.h:55-57, raised in
#             bcos-txpool/bcos-txpool/txpool/storage/MemoryStorage.cpp verifyAndSubmit):
#               10001 BlockLimitCheckFail  blockLimit already behind the chain
#               10000 NonceCheckFail       a nonce the chain has seen
#               10002 TxPoolIsFull         [txpool] limit reached on the receiving node
#             tx_gas_limit is not an admission check for tars transactions
#             (bcos-tx-validator CheckSet.h: the Bcos column has no MaxGasLimit, so
#             10023 never reaches a client); a creation that needs more than the limit fails
#             at execution with a non-zero receipt status (12 OutOfGas on the CI chains,
#             TransactionStatus.h:38; the limit's value is what it runs out against). A one-slot pool on an observer also answers 10010
#             TransactionPoolTimeout for the transaction it did admit. New scenario, no Java
#             counterpart.
# @ file    : admission.sh

# wait_node_answers <node_dir> <seconds>: the node's socket answers getBlockNumber
wait_node_answers()
{
    local waited=0 height
    while (( waited < ${2} )); do
        if height=$("${fisco_bcos_path}" attach getBlockNumber --node-dir "${1}" --json 2>/dev/null) && [[ "${height}" =~ ^[0-9]+$ ]]; then
            echo "${height}"
            return 0
        fi
        sleep 1
        waited=$((waited + 1))
    done
    scenario_fail "$(basename "${1}") does not answer after ${2}s"
    return 1
}

# txpool_full_probe <chain> <node_dir> <contract> <abi>: restart the expanded node with
# [txpool] limit=1 and push six signed transactions at it at once; at least one must be
# refused with 10002. The node is an observer since scenario 14, so stopping it costs no
# quorum; its config is restored afterwards.
txpool_full_probe()
{
    local chain="${1}" node_dir="${2}" contract="${3}" abi="${4}"
    local extra
    extra=$(chain_node_dir "${chain}" "$(chain_node_count "${chain}")")
    [[ -f "${extra}/config.ini" ]] || { scenario_fail "no expanded node at ${extra}"; return 1; }
    bash "${extra}/stop.sh" >/dev/null 2>&1
    cp "${extra}/config.ini" "${extra}/config.ini.scenario15"
    sed -i.bak 's/^\([[:space:]]*\)limit=[0-9]*/\1limit=1/' "${extra}/config.ini" && rm -f "${extra}/config.ini.bak"
    /usr/bin/grep -q '^[[:space:]]*limit=1$' "${extra}/config.ini" || { scenario_fail "could not set [txpool] limit=1 in ${extra}/config.ini"; return 1; }
    bash "${extra}/start.sh" >/dev/null 2>&1
    wait_node_answers "${extra}" 60 >/dev/null || return 1
    local work
    work=$(mktemp -d "${TMPDIR:-/tmp}/ci-txpool-${chain}.XXXXXX") || return 1
    local i raw
    for i in 1 2 3 4 5 6; do
        raw=$("${fisco_bcos_path}" tx sign "${contract}" --abi "${abi}" ok --node-dir "${extra}" --json | jq -r .raw) || { rm -rf "${work}"; return 1; }
        echo "${raw}" > "${work}/raw${i}"
    done
    for i in 1 2 3 4 5 6; do
        ( "${fisco_bcos_path}" tx raw "@${work}/raw${i}" --node-dir "${extra}" --json > "${work}/out${i}" 2>&1 ) &
    done
    wait
    # 10010 TransactionPoolTimeout is the other way a one-slot pool on an observer answers: the
    # single admitted transaction waits for a sealer to pull it and the submit times out first
    local full=0 accepted=0 timed_out=0 other=""
    for i in 1 2 3 4 5 6; do
        case "$(jq -r 'if .error then (.error.code | tostring) else "status " + (.status | tostring) end' "${work}/out${i}" 2>/dev/null)" in
            10002) full=$((full + 1)) ;;
            10010) timed_out=$((timed_out + 1)) ;;
            "status 0") accepted=$((accepted + 1)) ;;
            *) other="${other} $(head -c 200 "${work}/out${i}")" ;;
        esac
    done
    rm -rf "${work}"
    # back to the normal limit before judging, so a failure here never leaves a crippled node
    bash "${extra}/stop.sh" >/dev/null 2>&1
    mv "${extra}/config.ini.scenario15" "${extra}/config.ini"
    bash "${extra}/start.sh" >/dev/null 2>&1
    wait_node_answers "${extra}" 60 >/dev/null || return 1
    echo "scenario 15: txpool limit=1 on $(basename "${extra}"): ${full} x 10002 TxPoolIsFull, ${accepted} accepted, ${timed_out} x 10010 TransactionPoolTimeout${other:+, other:${other}}"
    (( full >= 1 )) || { scenario_fail "no TxPoolIsFull among six concurrent submissions"; return 1; }
    [[ -z "${other}" ]] || { scenario_fail "unexpected outcomes:${other}"; return 1; }
}

scenario_15_admission()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 15 admission "new scenario: blockLimit / nonce / tx_gas_limit / txpool.limit refusals"
    local abi deploy contract
    abi=$(contract_abi Reverter) || return 1
    deploy=$(ops "${node_dir}" tx deploy "$(contract_bin Reverter "${chain}")" --abi "${abi}") || { echo "${deploy}"; return 1; }
    assert_jq "${deploy}" '.status == 0' "deploy Reverter for the admission probes" || return 1
    contract=$(jq_get "${deploy}" '.contractAddress')
    # 1. blockLimit behind the chain
    local height refused
    height=$(ops "${node_dir}" attach getBlockNumber) || return 1
    refused=$(ops_expect_exit 2 "${node_dir}" tx send "${contract}" --abi "${abi}" ok --block-limit $((height - 1))) || return 1
    assert_jq "${refused}" '.error.code == 10001 and .error.message == "BlockLimitCheckFail"' "blockLimit ${height}-1 is refused with 10001" || return 1
    # 2. a nonce the chain has already seen
    local nonce sent
    nonce="15$(date +%s)$$"
    sent=$(ops "${node_dir}" tx send "${contract}" --abi "${abi}" ok --nonce "${nonce}") || { echo "${sent}"; return 1; }
    assert_jq "${sent}" '.status == 0' "first use of nonce ${nonce}" || return 1
    refused=$(ops_expect_exit 2 "${node_dir}" tx send "${contract}" --abi "${abi}" ok --nonce "${nonce}") || return 1
    assert_jq "${refused}" '.error.code == 10000 and .error.message == "NonceCheckFail"' "the replayed nonce is refused with 10000" || return 1
    # 3. tx_gas_limit: a creation that needs more than the limit fails at execution
    if [[ "${chain}" == "auth" ]]; then
        echo "scenario 15: tx_gas_limit probe skipped on ${chain}: a tx_gas_limit set through a committee proposal breaks every later EVM call on this release (README, node-side findings)"
    else
        local governor current limited restored
        governor=$(chain_governor_pem "${node_dir}")
        [[ -f "${governor}" ]] || { scenario_fail "no governor pem for ${node_dir}"; return 1; }
        current=$(ops "${node_dir}" sysconfig get tx_gas_limit) || return 1
        current=$(jq_get "${current}" '.value')
        # 100000 is the smallest value SystemConfigPrecompiled accepts; ComplexSol's creation
        # needs more than that, Reverter.ok() (~31k) still fits
        limited=$(ops "${node_dir}" sysconfig set tx_gas_limit 100000 --yes --account "${governor}") || { echo "${limited}"; return 1; }
        assert_jq "${limited}" '.ok == true' "tx_gas_limit lowered to 100000" || return 1
        local ctor_abi heavy
        ctor_abi=$(contract_abi_without_constructor ComplexSol) || return 1
        heavy=$(ops_expect_exit 2 "${node_dir}" tx deploy "$(cat "$(contract_bin ComplexSol "${chain}")")${c_complex_ctor_args}" --abi "${ctor_abi}") || return 1
        local over_status
        over_status=$(jq_get "${heavy}" '.status')
        assert_jq "${heavy}" '.status != 0 and (.gasUsed | tonumber) <= 100000' "a creation over the gas limit fails (status ${over_status})" || return 1
        sent=$(ops "${node_dir}" tx send "${contract}" --abi "${abi}" ok) || { echo "${sent}"; return 1; }
        assert_jq "${sent}" '.status == 0 and (.gasUsed | tonumber) < 100000' "a call under the limit still runs" || return 1
        restored=$(ops "${node_dir}" sysconfig set tx_gas_limit "${current}" --yes --account "${governor}") || { echo "${restored}"; return 1; }
        assert_jq "${restored}" '.ok == true' "tx_gas_limit restored to ${current}" || return 1
        heavy=$(ops "${node_dir}" tx deploy "$(cat "$(contract_bin ComplexSol "${chain}")")${c_complex_ctor_args}" --abi "${ctor_abi}" --timeout 60000) || { echo "${heavy}"; return 1; }
        assert_jq "${heavy}" '.status == 0' "the same creation deploys once the limit is back" || return 1
        echo "scenario 15: creation over tx_gas_limit 100000 failed with status ${over_status} before, 0 after restoring ${current}"
    fi
    # 4. txpool.limit on the receiving node
    txpool_full_probe "${chain}" "${node_dir}" "${contract}" "${abi}" || return 1
    echo "scenario 15 ok: 10001 / 10000 / gas limit / 10002 on ${chain}"
}
