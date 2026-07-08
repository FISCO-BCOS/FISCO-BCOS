#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: genesis bootstrap. Assert that all 13 predeploys are present in the
# genesis state (non-empty runtime code at their canonical addresses) and that
# SystemConfig.getValueByKey("chain_id") returns a well-formed 2-word result
# with a nonzero chainId.
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

log "calling SystemConfig.getValueByKey(\"chain_id\")"
ret="$(eth_call "${SYSTEM_CONFIG_ADDR}" "${CALLDATA_GET_CHAIN_ID}")"
ret_len="$(hex_byte_len "${ret}")"
# getValueByKey returns (value uint192, enableNumber uint64) = 2 ABI words =
# 64 bytes. chain_id == 0 is rejected by the C++ L2ConfigLoader (breaks
# EIP-155), so a zero word 0 here means the genesis allocs never seeded the
# chain_id slot.
if [[ "${ret_len}" -ne 64 ]]; then
    fail "getValueByKey(\"chain_id\") returned ${ret_len} bytes, expected 64"
fi
word0="$(hex_body "${ret}")"
word0="${word0:0:64}"
if [[ "${word0}" =~ ^0+$ ]]; then
    fail "getValueByKey(\"chain_id\") returned chainId == 0 (chain_id slot not seeded)"
fi
log "getValueByKey(\"chain_id\") returned ${ret_len} bytes, chainId word nonzero"

log "PASS"
