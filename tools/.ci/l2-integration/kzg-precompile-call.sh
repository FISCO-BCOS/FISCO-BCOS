#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: EIP-4844 point-evaluation precompile (0x0a). A6.14 registers the
# c-kzg-4844 point-evaluation precompile at address 0x0a in L2 mode. eth_call
# it with the 192-byte infinity-point known-answer vector and assert the
# 64-byte FIELD_ELEMENTS_PER_BLOB || BLS_MODULUS result.
#
# The vector is copied verbatim from
# bcos-executor/test/unittest/test_KzgPrecompileRegistered.cpp (KzgKnownAnswer):
#   input  = versioned_hash(32) || z(32) || y(32) || commitment(48) || proof(48)
#   output = FIELD_ELEMENTS_PER_BLOB (0x1000) || BLS_MODULUS  (64 bytes)
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

require_devnet
require_cmd curl jq
wait_for_rpc

# 192-byte input (commitment = proof = G1 infinity 0xc0||47*00; z = y = 0;
# versioned_hash = sha256(commitment) with byte[0]=0x01).
KZG_INPUT="0x010657f37554c781402a22917dee2f75def7ab966d7b770905398eba3c44401400000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"

# 64-byte success output.
KZG_EXPECTED="0x000000000000000000000000000000000000000000000000000000000000100073eda753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001"

log "calling KZG point-evaluation precompile at ${KZG_PRECOMPILE_ADDR}"
# sanity: input must be 192 bytes.
in_len="$(hex_byte_len "${KZG_INPUT}")"
[[ "${in_len}" -eq 192 ]] || fail "KZG input is ${in_len} bytes, expected 192"

ret="$(eth_call "${KZG_PRECOMPILE_ADDR}" "${KZG_INPUT}")"
ret_norm="$(lc "${ret}")"
exp_norm="$(lc "${KZG_EXPECTED}")"

ret_len="$(hex_byte_len "${ret}")"
if [[ "${ret_len}" -ne 64 ]]; then
    fail "KZG precompile returned ${ret_len} bytes, expected 64 (result: ${ret})"
fi
if [[ "${ret_norm}" != "${exp_norm}" ]]; then
    fail "KZG result mismatch:\n  got      ${ret}\n  expected ${KZG_EXPECTED}"
fi

log "KZG precompile returned the expected 64-byte FIELD_ELEMENTS_PER_BLOB || BLS_MODULUS"
log "PASS"
