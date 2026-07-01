#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: genesis bootstrap. Assert that all 13 predeploys are present in the
# genesis state (non-empty runtime code at their canonical addresses) and that
# SystemConfig.getChainConfig() returns a well-formed (>= 4-word) result.
#
# Fully automated once the A8 devnet is up. SKIPs (exit 77) if the devnet
# compose file is absent or the RPC never comes up.
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_devnet
require_cmd curl jq
wait_for_rpc

log "checking eth_getCode for the 13 predeploys"
for addr in "${ALL_PREDEPLOYS[@]}"; do
    code="$(eth_get_code "${addr}")"
    body="$(hex_body "${code}")"
    if [[ -z "${body}" ]]; then
        fail "predeploy ${addr} has empty code (genesis allocs did not seed it)"
    fi
    log "  ${addr} -> $(hex_byte_len "${code}") bytes"
done
log "all 13 predeploys carry non-empty runtime code"

log "calling SystemConfig.getChainConfig() (${SELECTOR_GET_CHAIN_CONFIG})"
ret="$(eth_call "${SYSTEM_CONFIG_ADDR}" "${SELECTOR_GET_CHAIN_CONFIG}")"
ret_len="$(hex_byte_len "${ret}")"
# getChainConfig() returns (chainId, l2BlockGasLimit, compatibilityVersion,
# featureFlags) = 4 ABI words = 128 bytes. The C++ L2ConfigLoader aborts the
# block on a return < 128 bytes, so the integration check uses the same bound.
if [[ "${ret_len}" -lt 128 ]]; then
    fail "getChainConfig() returned ${ret_len} bytes, expected >= 128"
fi
log "getChainConfig() returned ${ret_len} bytes (>= 128)"

log "PASS"
