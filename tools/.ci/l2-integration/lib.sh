#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Shared helpers for the L2 integration scenarios. Sourced by every scenario
# script and by run-all.sh. Defines the devnet-presence guard, the JSON-RPC
# request helpers, and the distinct SKIP exit code.
#
# Exit-code contract (consumed by run-all.sh):
#   0   scenario passed
#   77  scenario SKIPPED (devnet absent, or a sub-step blocked on A8 tooling)
#   *   any other non-zero -> scenario FAILED
set -euo pipefail

# Distinct from any real failure so run-all.sh can report SKIPPED, not FAIL.
SKIP_EXIT=77

# Repo root = four levels up from this file (tools/.ci/l2-integration/lib.sh).
_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${_LIB_DIR}/../../.." && pwd)"

# The A8 devnet compose file. Its presence is the single gate for every
# scenario that needs a live chain.
DEVNET_COMPOSE="${DEVNET_COMPOSE:-${REPO_ROOT}/tools/.ci/l2-devnet/docker-compose.yml}"

# JSON-RPC endpoint of the L2 node (eth_* Web3 RPC). Override via env.
L2_RPC_URL="${L2_RPC_URL:-http://127.0.0.1:8545}"

log()  { printf '[%s] %s\n' "$(basename "${0}")" "$*"; }
fail() { log "FAIL: $*" >&2; exit 1; }
skip() { log "SKIP: $*" >&2; exit "${SKIP_EXIT}"; }

# lowercase for hex comparisons (portable to bash 3.2 — ${var,,} is bash 4+)
lc() { printf '%s' "$1" | tr 'A-F' 'a-f'; }

# Fail-fast guard: if the A8 devnet is absent, SKIP with the distinct code.
require_devnet() {
    if [[ ! -f "${DEVNET_COMPOSE}" ]]; then
        skip "requires A8 devnet (tools/.ci/l2-devnet)"
    fi
}

# Assert a binary is on PATH or SKIP (these tools come from the devnet image in
# CI; locally the operator installs them).
require_cmd() {
    local missing=0 c
    for c in "$@"; do
        if ! command -v "${c}" >/dev/null 2>&1; then
            log "missing required command: ${c}" >&2
            missing=1
        fi
    done
    [[ "${missing}" -eq 0 ]] || skip "missing required tooling: $*"
}

# Single JSON-RPC call. Args: METHOD PARAMS_JSON_ARRAY. Echoes the raw response.
rpc() {
    local method="$1" params="${2:-[]}" out
    out="$(curl -sS -X POST \
        -H 'Content-Type: application/json' \
        --data "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"${method}\",\"params\":${params}}" \
        "${L2_RPC_URL}")" || fail "RPC transport error to ${L2_RPC_URL} (curl exit $?)"
    printf '%s' "${out}"
}

# Extract .result from a JSON-RPC response, failing loudly on a JSON-RPC error.
rpc_result() {
    local method="$1" params="${2:-[]}" resp
    resp="$(rpc "${method}" "${params}")"
    if echo "${resp}" | jq -e '.error' >/dev/null 2>&1; then
        fail "${method} returned RPC error: $(echo "${resp}" | jq -c '.error')"
    fi
    echo "${resp}" | jq -r '.result'
}

# eth_call with default tag "latest". Args: TO_ADDR DATA_HEX.
eth_call() {
    local to="$1" data="$2"
    rpc_result eth_call "[{\"to\":\"${to}\",\"data\":\"${data}\"},\"latest\"]"
}

# eth_getCode at "latest". Arg: ADDR.
eth_get_code() {
    rpc_result eth_getCode "[\"$1\",\"latest\"]"
}

# Strip a leading 0x. Echoes the hex body (possibly empty).
hex_body() {
    local h="${1#0x}"
    echo "${h}"
}

# Number of bytes encoded in an 0x-prefixed hex string.
hex_byte_len() {
    local body
    body="$(hex_body "$1")"
    echo $(( ${#body} / 2 ))
}

# Block until L2_RPC_URL answers eth_blockNumber, or SKIP after a timeout.
# Arg (optional): max attempts (default 30, ~30s).
wait_for_rpc() {
    local max="${1:-30}" i=0 bn
    while [[ "${i}" -lt "${max}" ]]; do
        if bn="$(rpc_result eth_blockNumber 2>/dev/null)" && [[ "${bn}" == 0x* ]]; then
            log "RPC up at ${L2_RPC_URL} (blockNumber=${bn})"
            return 0
        fi
        i=$(( i + 1 ))
        sleep 1
    done
    skip "RPC at ${L2_RPC_URL} did not come up within ${max}s (devnet not running)"
}

# --- L2 protocol constants (single source of truth for the scenarios) --------

# SystemConfig.getValueByKey(string) calldata for key "chain_id". The string
# arg is a dynamic ABI type, so we carry the full pre-encoded calldata rather
# than hand-assembling head/len/padding in shell. Regenerate with:
#   cast calldata 'getValueByKey(string)' 'chain_id'
# Returns (value uint192, enableNumber uint64) = 2 ABI words = 64 bytes;
# word 0 (low bits) is the chainId.
CALLDATA_GET_CHAIN_ID="0x1258a93a00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000008636861696e5f6964000000000000000000000000000000000000000000000000"
# Ownable.owner() selector.
SELECTOR_OWNER="0x8da5cb5b"

# The 13 canonical predeploy addresses (full 40-hex, 0x-prefixed).
SYSTEM_CONFIG_ADDR="0x43000000000000000000000000000000000000C0"
L2_VALIDATOR_SET_ADDR="0x43000000000000000000000000000000000000C1"
L1BLOCK_ADDR="0x4200000000000000000000000000000000000015"
L2_TO_L1_MESSAGE_PASSER_ADDR="0x4200000000000000000000000000000000000016"
L2_CROSS_DOMAIN_MESSENGER_ADDR="0x4200000000000000000000000000000000000007"
L2_STANDARD_BRIDGE_ADDR="0x4200000000000000000000000000000000000010"
GAS_PRICE_ORACLE_ADDR="0x420000000000000000000000000000000000000F"
PROXY_ADMIN_ADDR="0x4200000000000000000000000000000000000018"
SEQUENCER_FEE_VAULT_ADDR="0x4200000000000000000000000000000000000011"
BASE_FEE_VAULT_ADDR="0x4200000000000000000000000000000000000019"
L1_FEE_VAULT_ADDR="0x420000000000000000000000000000000000001A"
WETH_ADDR="0x4200000000000000000000000000000000000006"
OPTIMISM_MINTABLE_ERC20_FACTORY_ADDR="0x4200000000000000000000000000000000000012"

# All 13, for loops.
ALL_PREDEPLOYS=(
    "${L1BLOCK_ADDR}"
    "${L2_TO_L1_MESSAGE_PASSER_ADDR}"
    "${L2_CROSS_DOMAIN_MESSENGER_ADDR}"
    "${L2_STANDARD_BRIDGE_ADDR}"
    "${GAS_PRICE_ORACLE_ADDR}"
    "${PROXY_ADMIN_ADDR}"
    "${SEQUENCER_FEE_VAULT_ADDR}"
    "${BASE_FEE_VAULT_ADDR}"
    "${L1_FEE_VAULT_ADDR}"
    "${WETH_ADDR}"
    "${OPTIMISM_MINTABLE_ERC20_FACTORY_ADDR}"
    "${SYSTEM_CONFIG_ADDR}"
    "${L2_VALIDATOR_SET_ADDR}"
)

# The three FISCO-private precompiles that disabledInL2() hides (full 40-hex).
SYS_CONFIG_PRECOMPILE="0x0000000000000000000000000000000000001000"
TABLE_PRECOMPILE="0x0000000000000000000000000000000000001001"
CONSENSUS_PRECOMPILE="0x0000000000000000000000000000000000001003"

# EIP-4844 point-evaluation precompile.
KZG_PRECOMPILE_ADDR="0x000000000000000000000000000000000000000a"
