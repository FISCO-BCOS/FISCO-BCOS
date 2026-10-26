#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 8 balance: turn the balance feature on (two system config writes;
#             two committee proposals on the auth chain), then add / get / transfer / sub /
#             list-caller with the chain's governor account. Replaces java-sdk-demo
#             java_sdk_demo_ci_test.sh contract_test (227-254: feature_balance,
#             feature_balance_precompiled, addBalance) and the old ci_check_air.sh
#             console addBalance top-up (402-427).
# @ file    : balance.sh

scenario_8_balance()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 8 balance "java-sdk-demo contract_test:227-254, ci_check_air.sh addBalance:402-427"
    local governor
    governor=$(chain_governor_pem "${node_dir}")
    [[ -f "${governor}" ]] || { scenario_fail "no governor pem for ${node_dir}"; return 1; }
    local governor_addr
    governor_addr=$(pem_address "${governor}")
    local enable
    enable=$(ops "${node_dir}" balance enable --yes --account "${governor}") || { echo "${enable}"; return 1; }
    local features
    features=$(ops "${node_dir}" sysconfig features) || return 1
    assert_jq "${features}" 'map(select(.name == "feature_balance" or .name == "feature_balance_precompiled") | .enabled) == [true, true]' \
        "feature_balance and feature_balance_precompiled on" || return 1
    local other created
    local sm_flag=""
    [[ "${chain}" == "sm" ]] && sm_flag="--sm"
    # capture, then parse: in a pipe the exit code is jq's, which is 0 on empty input
    created=$("${fisco_bcos_path}" account new ${sm_flag} --out "$(dirname "${node_dir}")/../scenario8-acct" --json) || return 1
    other=$(jq -r .address <<<"${created}")
    # a system config value applies from the block after the one that set it, and a read-only
    # call runs against the current head: the first transaction (add) lands in a block where
    # both flags are on, the list-caller call after it sees the registered precompiled
    local result balance
    result=$(ops "${node_dir}" balance add "${other}" 1000 --yes --account "${governor}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.ok == true and .status == 0' "balance add 1000" || return 1
    # enabling at runtime registers the auth admin (genesis auth_admin_account) as caller
    local callers
    callers=$(ops "${node_dir}" balance list-caller) || return 1
    assert_jq "${callers}" '.value | map(ascii_downcase) | index("'"${governor_addr,,}"'") != null' \
        "governor ${governor_addr} is a balance caller" || return 1
    balance=$(ops "${node_dir}" balance get "${other}") || return 1
    assert_jq "${balance}" '.value == "1000"' "balance get after add" || return 1
    result=$(ops "${node_dir}" balance transfer "${other}" "${governor_addr}" 300 --yes --account "${governor}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.ok == true' "balance transfer 300" || return 1
    balance=$(ops "${node_dir}" balance get "${other}") || return 1
    assert_jq "${balance}" '.value == "700"' "balance get after transfer" || return 1
    balance=$(ops "${node_dir}" balance get "${governor_addr}") || return 1
    assert_jq "${balance}" '(.value | tonumber) >= 300' "governor received the transfer" || return 1
    result=$(ops "${node_dir}" balance sub "${other}" 200 --yes --account "${governor}") || { echo "${result}"; return 1; }
    assert_jq "${result}" '.ok == true' "balance sub 200" || return 1
    balance=$(ops "${node_dir}" balance get "${other}") || return 1
    assert_jq "${balance}" '.value == "500"' "balance get after sub" || return 1
    # an account that is not a caller is refused by the precompiled (receipt status != 0)
    local denied other_pem
    other_pem="$(dirname "${node_dir}")/../scenario8-acct/${other}.pem"
    denied=$(ops_expect_exit 2 "${node_dir}" balance add "${other}" 1 --yes --account "${other_pem}") || return 1
    assert_jq "${denied}" '.ok == false and .status != 0' "non-caller add is refused" || return 1
    if [[ "${chain}" == "auth" ]]; then
        # the governor registers a second caller (spec: auth chains register-caller first);
        # registerCaller/unregisterCaller are Governor-only direct calls (BalancePrecompiled.cpp)
        result=$(ops "${node_dir}" balance register-caller "${other}" --yes --account "${governor}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.ok == true' "balance register-caller" || return 1
        callers=$(ops "${node_dir}" balance list-caller) || return 1
        assert_jq "${callers}" '.value | map(ascii_downcase) | index("'"${other,,}"'") != null' "registered caller listed" || return 1
        result=$(ops "${node_dir}" balance add "${governor_addr}" 5 --yes --account "${other_pem}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.ok == true and .status == 0' "the registered caller may add" || return 1
        result=$(ops "${node_dir}" balance unregister-caller "${other}" --yes --account "${governor}") || { echo "${result}"; return 1; }
        assert_jq "${result}" '.ok == true' "balance unregister-caller" || return 1
        callers=$(ops "${node_dir}" balance list-caller) || return 1
        assert_jq "${callers}" '.value | map(ascii_downcase) | index("'"${other,,}"'") == null' "unregistered caller gone" || return 1
    fi
    echo "scenario 8 ok: balance add/transfer/sub by ${governor_addr} on ${chain}"
}
