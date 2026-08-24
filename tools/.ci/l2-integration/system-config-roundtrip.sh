#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: SystemConfig <-> RPC chainId consistency. Decode chainId from
# SystemConfig.getValueByKey("chain_id") word 0 (value uint192) and compare it
# against the eth_chainId RPC result. They MUST agree: PR-4's L2ConfigLoader
# reads the same SystemConfig._config slot directly (no EVM) onto
# LedgerConfig.chainId, and PR-6 wired eth_chainId to read that same
# LedgerConfig field. A mismatch means the two read paths diverged.
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_devnet
require_cmd curl jq
wait_for_rpc

log "reading chainId from SystemConfig.getValueByKey(\"chain_id\") word 0"
ret="$(eth_call "${SYSTEM_CONFIG_ADDR}" "${CALLDATA_GET_CHAIN_ID}")"
body="$(hex_body "${ret}")"
if [[ "${#body}" -lt 64 ]]; then
    fail "getValueByKey(\"chain_id\") result too short to contain a chainId word: 0x${body}"
fi
# Word 0 is the first 32 bytes (64 hex chars): the uint192 value, left-padded
# to a full word. Strip leading zeros to a canonical lowercase hex value.
word0="${body:0:64}"
word0_trimmed="$(echo "${word0}" | sed -E 's/^0+//')"
[[ -n "${word0_trimmed}" ]] || word0_trimmed=0
sc_chainid="0x${word0_trimmed}"
log "  SystemConfig chainId = ${sc_chainid}"

log "reading eth_chainId from RPC"
rpc_chainid="$(rpc_result eth_chainId)"
# Normalize: lowercase, strip leading zeros after 0x.
rpc_trimmed="$(echo "${rpc_chainid#0x}" | sed -E 's/^0+//')"
[[ -n "${rpc_trimmed}" ]] || rpc_trimmed=0
rpc_chainid="0x${rpc_trimmed}"
log "  eth_chainId            = ${rpc_chainid}"

if [[ "$(lc "${sc_chainid}")" != "$(lc "${rpc_chainid}")" ]]; then
    fail "chainId mismatch: SystemConfig=${sc_chainid} eth_chainId=${rpc_chainid} (PR-4/PR-6 read paths diverged)"
fi

log "chainId agrees across SystemConfig and eth_chainId (${rpc_chainid})"
log "PASS"
