#!/usr/bin/env bash
# "Copyright [2026] <fisco-bcos>"
# @ function: helpers shared by the native CI scenarios (tools/.ci/scenarios/*.sh): the ops
#             command wrapper, jq assertions and the scenario banner. Sourced by
#             ci_check_air.sh; every scenario_<n>_<name> <chain> <node_dir> returns non-zero
#             on its first failed assertion.
# @ file    : common.sh

# ${fisco_bcos_path} is set by ci_check_air.sh (FISCO_BCOS_BIN or the build tree binary)
scenario_banner()
{
    # scenario_banner <n> <name> <replaced Java methods>
    echo -e "\033[32m=== scenario ${1} ${2} (replaces ${3}) ===\033[0m"
}

scenario_fail()
{
    echo -e "\033[31m[scenario] FAIL: ${*}\033[0m" >&2
    return 1
}

# ops <node_dir> <args...>: run one ops subcommand against a node directory in JSON mode,
# stdout = the JSON document (stderr is kept apart so jq never sees a warning line, and is
# shown only when the command fails); the exit code is the command's
ops()
{
    local node_dir="${1}"
    shift
    local output rc errfile
    errfile=$(mktemp)
    output=$("${fisco_bcos_path}" "$@" --node-dir "${node_dir}" --json 2>"${errfile}") && rc=0 || rc=$?
    echo "${output}"
    if (( rc != 0 )); then
        echo -e "\033[31m[scenario] fisco-bcos $* exited ${rc}: $(head -c 800 <<<"${output}") $(head -c 400 "${errfile}")\033[0m" >&2
    fi
    rm -f "${errfile}"
    return "${rc}"
}

# ops_expect_exit <expected> <node_dir> <args...>: like ops, but the exit code must equal
# <expected>; the JSON is printed either way
ops_expect_exit()
{
    local expected="${1}"
    local node_dir="${2}"
    shift 2
    local output rc errfile
    errfile=$(mktemp)
    output=$("${fisco_bcos_path}" "$@" --node-dir "${node_dir}" --json 2>"${errfile}") && rc=0 || rc=$?
    echo "${output}"
    if [[ "${rc}" != "${expected}" ]]; then
        scenario_fail "fisco-bcos $* exited ${rc}, expected ${expected}: ${output} $(head -c 400 "${errfile}")"
        rm -f "${errfile}"
        return 1
    fi
    rm -f "${errfile}"
}

# assert_jq <json-text> <jq filter that must be truthy> <description>
assert_jq()
{
    local json="${1}"
    local filter="${2}"
    local what="${3}"
    if ! jq -e "${filter}" >/dev/null 2>&1 <<<"${json}"; then
        scenario_fail "${what}: jq '${filter}' is false on: $(head -c 600 <<<"${json}")"
        return 1
    fi
}

# assert_checks_but_view_change <json> <exit-code> <what> <allowed-states>: status and log status
# exit 2 when a check is in state "fail". The only failure a healthy idle chain shows is
# consensus_stable: a fresh chain stays in timeout until its first block, and until #5643 every
# block is followed by one consensus-timeout view change about one timeout later; a snapshot can
# land inside either. Passes when the exit code is 0 or 2 and every check is in one of
# <allowed-states> ("ok" for the RPC snapshot, "ok skipped" for the log one), consensus_stable
# also being allowed "fail" when the exit code is 2.
assert_checks_but_view_change()
{
    local json="${1}" rc="${2}" what="${3}" allowed="${4}"
    if (( rc != 0 && rc != 2 )); then
        scenario_fail "${what}: exit ${rc}"
        return 1
    fi
    if ! jq -e --arg allowed "${allowed}" --argjson rc "${rc}" '[.checks[]
            | select(.state as $s | ($allowed | split(" ") | index($s)) == null)
            | select($rc != 2 or .name != "consensus_stable" or .state != "fail")] == []' \
            >/dev/null 2>&1 <<<"${json}"; then
        scenario_fail "${what}: $(jq -c '[.checks[] | {name,state,detail}]' <<<"${json}" 2>&1 | head -c 600)"
        return 1
    fi
}

# jq_get <json-text> <filter>: raw value
jq_get()
{
    jq -r "${2}" <<<"${1}"
}

# group id of the chain behind <node_dir>, from the status snapshot
chain_group_id()
{
    ops "${1}" status | jq -r '.identity.groupId'
}

# admin account pem of an auth chain built by ci_check_air.sh (nodes/<chain>/admin/<0x..>.pem)
chain_admin_pem()
{
    ls "${1}"/../../admin/0x*.pem 2>/dev/null | /usr/bin/grep -v public | head -n 1
}

# the vendored CI contracts (tools/.ci/contracts/<Name>/<Name>.{bin,sm.bin,abi})
contracts_dir()
{
    echo "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/contracts"
}

# contract_bin <Name> <chain>: the keccak or SM bytecode file for the chain's hash
contract_bin()
{
    if [[ "${2}" == "sm" ]]; then
        echo "$(contracts_dir)/${1}/${1}.sm.bin"
    else
        echo "$(contracts_dir)/${1}/${1}.bin"
    fi
}

# contract_abi <Name>: the vendored solc ABI. `tx deploy` accepts an ABI without a
# constructor entry (bytecode alone, 85e24b2bd) and encodes constructor arguments without
# the selector the cpp-sdk used to prepend (aa9550cd2), so no patched copy is needed.
contract_abi()
{
    echo "$(contracts_dir)/${1}/${1}.abi"
}

# chain_governor_pem <node_dir>: the account allowed to govern the chain behind <node_dir>:
# the -a admin of an auth chain (nodes/<chain>/admin/), else the admin build_chain generated
# into ca/accounts (ca/accounts_gm on an SM chain), which genesis names as auth_admin_account
# and the balance precompiled registers as the first caller
chain_governor_pem()
{
    local pem
    pem=$(chain_admin_pem "${1}")
    [[ -n "${pem}" ]] || pem=$(ls "${1}"/../../ca/accounts*/0x*.pem 2>/dev/null | /usr/bin/grep -v public | head -n 1)
    echo "${pem}"
}

# pem_address <pem>: the 0x address encoded in the file name
pem_address()
{
    basename "${1}" .pem
}
