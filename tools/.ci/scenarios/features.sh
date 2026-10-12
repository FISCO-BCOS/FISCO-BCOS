#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: scenario 17 features: every feature_* / bugfix_* flag that is still off is
#             turned on through `sysconfig enable-features` (dependencies first; a committee
#             proposal per key on the auth chain) and read back as enabled. Runs last on
#             each chain: with everything on the chain behaves differently (rPBFT, sharding,
#             the EVM forks) and nothing after it would be comparable. A flag a chain refuses
#             is a failure unless it is in that chain's known list below. New scenario.
# @ file    : features.sh

# features_known_refusals <chain>: flags this chain's executor refuses, one per line
features_known_refusals()
{
    case "${1}" in
        *) echo "" ;;
    esac
}

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
    [[ -n "${pending}" ]] || { echo "scenario 17: every flag is already on"; return 0; }
    local known
    known=$(features_known_refusals "${chain}")
    local flag result enabled=() refused=() unexpected=()
    for flag in ${pending}; do
        if result=$(ops "${node_dir}" sysconfig enable-features "${flag}" --yes --account "${governor}" --timeout 60000 2>/dev/null); then
            enabled+=("${flag}")
        else
            refused+=("${flag}")
            echo "scenario 17: ${flag} refused on ${chain}: $(head -c 300 <<<"${result}")"
            /usr/bin/grep -qx "${flag}" <<<"${known}" || unexpected+=("${flag}")
        fi
    done
    listing=$(ops "${node_dir}" sysconfig features) || return 1
    local flag_json
    flag_json=$(printf '%s\n' "${enabled[@]}" | jq -R . | jq -s .)
    assert_jq "${listing}" 'map(select(.name as $n | '"${flag_json}"' | index($n)) | .enabled) | all' "every flag that reported ok reads back enabled" || return 1
    assert_jq "${listing}" '[.[] | select(.enabled == false and .deprecated != true) | .name] | length == '"${#refused[@]}" "only the refused flags stay off" || return 1
    (( ${#unexpected[@]} == 0 )) || { scenario_fail "flags refused on ${chain} that are not in features_known_refusals: ${unexpected[*]}"; return 1; }
    echo "scenario 17 ok: ${#enabled[@]} flags enabled on ${chain}${refused[*]:+, refused (known): ${refused[*]}}"
}
