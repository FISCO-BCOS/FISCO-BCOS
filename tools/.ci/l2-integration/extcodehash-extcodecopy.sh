#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: EXTCODEHASH / EXTCODECOPY observability for genesis predeploys.
#
# Achievable now (no funded key): for L1Block, SystemConfig and L2ValidatorSet,
# fetch eth_getCode and assert (a) the code is large for the heavyweight
# contracts (> 1KB), and (b) getCode is byte-identical across two consecutive
# calls (a code-less or non-deterministically-seeded account would not be).
# When `cast` is available, also compute keccak256(code) locally — the value an
# in-EVM EXTCODEHASH must equal.
#
# Blocked on A8: the in-EVM EXTCODEHASH/EXTCODECOPY assertion needs a deployed
# helper contract that runs those opcodes against the predeploy and returns the
# result, which requires a funded key. That path is TODO-A8.
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_devnet
require_cmd curl jq
wait_for_rpc

# (address, human name, min expected bytes). SystemConfig/L2ValidatorSet are
# the heavyweight self-written predeploys; L1Block is checked for stability
# only (it is small).
declare -a TARGETS=(
    "${L1BLOCK_ADDR}|L1Block|1"
    "${SYSTEM_CONFIG_ADDR}|SystemConfig|1024"
    "${L2_VALIDATOR_SET_ADDR}|L2ValidatorSet|1024"
)

HAVE_CAST=0
command -v cast >/dev/null 2>&1 && HAVE_CAST=1

for entry in "${TARGETS[@]}"; do
    IFS='|' read -r addr name minbytes <<< "${entry}"
    log "checking ${name} (${addr})"

    code1="$(eth_get_code "${addr}")"
    code2="$(eth_get_code "${addr}")"
    body1="$(hex_body "${code1}")"

    if [[ -z "${body1}" ]]; then
        fail "${name} has empty code"
    fi
    if [[ "${code1}" != "${code2}" ]]; then
        fail "${name} eth_getCode differs across two calls (non-deterministic state)"
    fi
    log "  getCode stable across two calls"

    nbytes="$(hex_byte_len "${code1}")"
    if [[ "${nbytes}" -lt "${minbytes}" ]]; then
        fail "${name} code is ${nbytes} bytes, expected >= ${minbytes}"
    fi
    log "  code length ${nbytes} bytes (>= ${minbytes})"

    if [[ "${HAVE_CAST}" -eq 1 ]]; then
        # keccak256 of the runtime code = the value EXTCODEHASH must return.
        codehash="$(cast keccak "${code1}")"
        log "  keccak256(code) = ${codehash}  (== expected EXTCODEHASH)"
    else
        log "  SKIP-NOTE: cast unavailable; cannot precompute keccak256(code)"
    fi
done

# TODO-A8: deploy a helper contract that executes EXTCODEHASH(addr) and
# EXTCODECOPY(addr,...) and returns them, then assert EXTCODEHASH == the
# keccak256(code) computed above and EXTCODECOPY bytes == eth_getCode. Needs a
# funded key from the A8 devnet.
log "SKIP-NOTE: in-EVM EXTCODEHASH/EXTCODECOPY assertion is TODO-A8 (needs deployable helper)"

log "PASS (getCode subset; in-EVM opcode check deferred to A8)"
