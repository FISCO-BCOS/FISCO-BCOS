#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 5 sysconfig: set tx_count_limit / tx_gas_limit and read them back;
#             on the auth chain the direct precompiled call answers NO_AUTHORIZED and the
#             committee proposal path (single governor, rates 0/0) executes at once.
#             Replaces console clientWithParamsTest (211-215) and java-sdk
#             PrecompiledTest.test3SystemConfigService.
# @ file    : sysconfig.sh

scenario_5_sysconfig()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 5 sysconfig "console clientWithParamsTest:211-215, java-sdk PrecompiledTest.test3SystemConfigService"
    local -a account=()
    local admin
    if [[ "${chain}" == "auth" ]]; then
        admin=$(chain_admin_pem "${node_dir}")
        [[ -f "${admin}" ]] || { scenario_fail "auth admin pem not found next to ${node_dir}"; return 1; }
        account=(--account "${admin}")
        # the committee precompiled refuses an EOA calling the system config contract
        local denied
        denied=$(ops_expect_exit 2 "${node_dir}" sysconfig set tx_count_limit 1500 --direct --yes "${account[@]}") || return 1
        assert_jq "${denied}" '.codeName == "NO_AUTHORIZED"' "--direct on auth chain is NO_AUTHORIZED" || return 1
    fi
    # tx_gas_limit follows java-sdk's testSystemConfigService: current value + 1. On the auth
    # chain the gas limit is left alone: once tx_gas_limit is set through a committee proposal
    # (any value, 3000000001 and 2999999999 tried), every EVM `call` on that chain fails with
    # status 12 "Execution out of gas." thrown by HostContext::consumeTransferGas
    # (transaction-executor/.../vm/HostContext.h:843), while a direct setValueByKey on a plain
    # chain does not; tracked as a node issue in tools/.ci/README.md.
    local current gasLimit
    current=$(ops "${node_dir}" sysconfig get tx_gas_limit) || return 1
    gasLimit=$(( $(jq_get "${current}" '.value') + 1 ))
    local -a entries=("tx_count_limit=1500" "tx_gas_limit=${gasLimit}")
    if [[ "${chain}" == "auth" ]]; then
        echo "scenario 5: tx_gas_limit not changed on the auth chain (proposal path breaks EVM calls, see README)"
        entries=("tx_count_limit=1500")
    fi
    local key value result readback
    for entry in "${entries[@]}"; do
        key="${entry%%=*}"
        value="${entry#*=}"
        result=$(ops "${node_dir}" sysconfig set "${key}" "${value}" --yes "${account[@]}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.ok == true' "sysconfig set ${key}" || return 1
        if [[ "${chain}" == "auth" ]]; then
            assert_jq "${result}" '.mode == "proposal"' "auth chain routes set to a proposal" || return 1
            assert_jq "${result}" '.proposal.statusName == "passed" and .proposal.executed == true' "single governor proposal executes" || return 1
        else
            assert_jq "${result}" '.mode == "direct"' "plain chain sets directly" || return 1
        fi
        readback=$(ops "${node_dir}" sysconfig get "${key}") || return 1
        assert_jq "${readback}" '.value == "'"${value}"'"' "sysconfig get ${key} reads ${value}" || return 1
        assert_jq "${readback}" '.enableNumber | type == "number"' "sysconfig get enableNumber" || return 1
    done
    echo "scenario 5 ok: tx_count_limit / tx_gas_limit set and read on ${chain}"
}
