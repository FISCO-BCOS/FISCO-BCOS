#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: disabled FISCO-private precompiles. In L2 mode disabledInL2()
# removes the 13 FISCO-private stateful precompiles from the executor's
# precompiled map. From the EVM's view those addresses become code-less
# accounts: eth_getCode returns 0x, and an eth_call to them returns empty
# (0x) with no revert data — identical to calling any never-deployed address.
# This checks 3 of the 13: SystemConfig (0x..1000), Table (0x..1001),
# Consensus (0x..1003).
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_devnet
require_cmd curl jq
wait_for_rpc

DISABLED=(
    "${SYS_CONFIG_PRECOMPILE}"
    "${TABLE_PRECOMPILE}"
    "${CONSENSUS_PRECOMPILE}"
)

for addr in "${DISABLED[@]}"; do
    log "checking disabled precompile ${addr}"

    code="$(eth_get_code "${addr}")"
    if [[ "${code}" != "0x" && -n "$(hex_body "${code}")" ]]; then
        fail "${addr} eth_getCode returned non-empty (${code}); precompile not disabled in L2"
    fi
    log "  eth_getCode -> 0x (code-less account)"

    # eth_call with the SystemConfig getter selector. Against a code-less
    # account the EVM returns empty output with success (no revert data) —
    # the same observable behavior as calling any address with no code.
    resp="$(rpc eth_call "[{\"to\":\"${addr}\",\"data\":\"${SELECTOR_GET_CHAIN_CONFIG}\"},\"latest\"]")"
    if echo "${resp}" | jq -e '.error' >/dev/null 2>&1; then
        # An empty-account call must NOT carry revert data. A populated error
        # object means the precompile is still reachable (still registered).
        err="$(echo "${resp}" | jq -c '.error')"
        fail "${addr} eth_call returned a revert/error (${err}); expected empty-account semantics"
    fi
    result="$(echo "${resp}" | jq -r '.result')"
    if [[ "${result}" != "0x" && -n "$(hex_body "${result}")" ]]; then
        fail "${addr} eth_call returned non-empty result (${result}); precompile still live"
    fi
    log "  eth_call -> 0x (empty, no revert)"
done

log "all 3 sampled precompiles behave as code-less accounts in L2"
log "PASS"
