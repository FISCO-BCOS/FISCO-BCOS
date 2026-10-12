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
# stdout = the JSON document; the exit code is the command's
ops()
{
    local node_dir="${1}"
    shift
    local output rc
    output=$("${fisco_bcos_path}" "$@" --node-dir "${node_dir}" --json 2>&1) && rc=0 || rc=$?
    echo "${output}"
    if (( rc != 0 )); then
        echo -e "\033[31m[scenario] fisco-bcos $* exited ${rc}: $(head -c 800 <<<"${output}")\033[0m" >&2
    fi
    return "${rc}"
}

# ops_expect_exit <expected> <node_dir> <args...>: like ops, but the exit code must equal
# <expected>; the JSON is printed either way
ops_expect_exit()
{
    local expected="${1}"
    local node_dir="${2}"
    shift 2
    local output rc
    output=$("${fisco_bcos_path}" "$@" --node-dir "${node_dir}" --json 2>&1) && rc=0 || rc=$?
    echo "${output}"
    if [[ "${rc}" != "${expected}" ]]; then
        scenario_fail "fisco-bcos $* exited ${rc}, expected ${expected}: ${output}"
        return 1
    fi
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

# contract_abi <Name>: the ABI file with an explicit default constructor. solc omits the
# implicit constructor from the ABI and the cpp-sdk codec behind `tx deploy` wants an entry
# for it, so a copy with one appended is written under $TMPDIR (noted in README; the
# vendored .abi stays the solc output so regenerate.sh --check keeps meaning something)
contract_abi()
{
    local abi="$(contracts_dir)/${1}/${1}.abi"
    local patched="${TMPDIR:-/tmp}/ci-abi-$$-${1}.abi"
    jq 'if any(.[]; .type == "constructor") then . else . + [{"inputs": [], "stateMutability": "nonpayable", "type": "constructor"}] end' \
        "${abi}" > "${patched}" || return 1
    echo "${patched}"
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
