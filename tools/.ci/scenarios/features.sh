#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 17 features: every feature_* / bugfix_* flag that is still off is
#             turned on through `sysconfig enable-features` (dependencies first; a committee
#             proposal per key on the auth chain) and read back as enabled. Runs last on
#             each chain: with everything on the chain behaves differently (rPBFT, sharding,
#             the EVM forks) and nothing after it would be comparable. One call, so the
#             dependency order of featureEnableOrder is exercised. New scenario.
# @ file    : features.sh

scenario_17_features()
{
    local chain="${1}"
    local node_dir="${2}"
    scenario_banner 17 features "new scenario: enable every remaining feature/bugfix flag"
    local governor
    governor=$(chain_governor_pem "${node_dir}")
    [[ -f "${governor}" ]] || { scenario_fail "no governor pem for ${node_dir}"; return 1; }
    local listing pending
    listing=$(ops "${node_dir}" sysconfig features) || return 1
    assert_jq "${listing}" 'length > 50 and all(.[]; has("name") and has("enabled"))' "sysconfig features lists the flags" || return 1
    pending=$(jq -r '.[] | select(.enabled == false and .deprecated != true) | .name' <<<"${listing}")
    if [[ -n "${pending}" ]]; then
        # one call with every off flag: featureEnableOrder puts each flag's prerequisites first
        # (already-on ones become "skipped" steps) and the run stops at the first refusal
        local result
        # shellcheck disable=SC2086
        result=$(ops "${node_dir}" sysconfig enable-features ${pending} --yes --account "${governor}" --timeout 60000) || {
            scenario_fail "enable-features refused on ${chain}: $(head -c 600 <<<"${result}")"; return 1; }
        assert_jq "${result}" '.ok == true and all(.steps[]; .action == "enabled" or .action == "skipped")' \
            "every step enabled or already on" || return 1
        # the two dependency edges Features::validate enforces, wherever both ends are steps
        assert_jq "${result}" '[.steps[].name] as $s
            | [["feature_balance","feature_balance_precompiled"],["feature_balance_precompiled","feature_balance_policy1"]]
            | all(.[]; (($s | index(.[1])) == null) or ((($s | index(.[0])) // 1e9) < ($s | index(.[1]))))' \
            "prerequisites come before the flags that need them" || return 1
    else
        echo "scenario 17: every flag is already on"
    fi
    listing=$(ops "${node_dir}" sysconfig features) || return 1
    assert_jq "${listing}" '[.[] | select(.enabled == false and .deprecated != true)] | length == 0' "every flag reads back enabled" || return 1
    # a read-back of "1" also passes on a chain that stopped sealing after the last flag: prove
    # it still executes and commits (runs whether or not anything was left to enable)
    local smoke
    # signed by the governor (the account that is allowed everything on every chain)
    smoke=$(ops "${node_dir}" tx smoke --timeout 60000 --account "${governor}") || return 1
    assert_jq "${smoke}" '.ok == true' "the chain still seals with every flag on" || return 1
    echo "scenario 17 ok: $(wc -w <<<"${pending}" | tr -d ' ') flags enabled on ${chain} in one ordered call"
}
