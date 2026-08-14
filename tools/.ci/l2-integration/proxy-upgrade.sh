#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Scenario: ProxyAdmin ownership + (TODO-A8) upgrade roundtrip.
#
# Automated now: read ProxyAdmin.owner() and assert it matches the expected
# proxy_admin_owner from the chain config (parameterized via env
# L2_PROXY_ADMIN_OWNER). This is the Phase B governance anchor — the DAO switch
# is a ProxyAdmin owner transfer, so confirming who owns ProxyAdmin at genesis
# is the precondition for every later upgrade.
#
# Blocked on A8: the full upgrade tx flow (deploy a new implementation, call
# ProxyAdmin.upgrade(proxy, impl), verify the proxy's implementation slot
# changed) needs a funded key + a deployable test impl, which the A8 devnet
# tooling provides. That path SKIPs until then.
set -euo pipefail
# shellcheck source=tools/.ci/l2-integration/lib.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

# Expected ProxyAdmin owner. Default 0x0: the OP ProxyAdmin predeploy ships
# with empty storage (owner slot 0 = zero address) until the op-deployer
# alloc merge seeds it; override per devnet genesis.
L2_PROXY_ADMIN_OWNER="${L2_PROXY_ADMIN_OWNER:-0x0000000000000000000000000000000000000000}"

require_devnet
require_cmd curl jq
wait_for_rpc

log "reading ProxyAdmin.owner() at ${PROXY_ADMIN_ADDR}"
ret="$(eth_call "${PROXY_ADMIN_ADDR}" "${SELECTOR_OWNER}")"
body="$(hex_body "${ret}")"
if [[ "${#body}" -lt 64 ]]; then
    fail "ProxyAdmin.owner() returned a short result: 0x${body}"
fi
# owner() returns an address right-padded into a 32-byte word; the address is
# the low 20 bytes = last 40 hex chars of word 0.
owner_hex="${body:24:40}"
owner="0x${owner_hex}"
log "  ProxyAdmin owner = ${owner}"

# Normalize both sides to lowercase, 40-hex, 0x-prefixed.
expected="${L2_PROXY_ADMIN_OWNER#0x}"
expected="$(printf '%040s' "${expected}" | tr ' ' '0')"
expected="0x${expected}"
if [[ "$(lc "${owner}")" != "$(lc "${expected}")" ]]; then
    fail "ProxyAdmin owner ${owner} != expected ${expected} (set L2_PROXY_ADMIN_OWNER to the genesis value)"
fi
log "ProxyAdmin owner matches expected proxy_admin_owner"

# --- Full upgrade roundtrip: blocked on A8 ----------------------------------
# TODO-A8: with a funded key the full flow is:
#   1. deploy a trivial new implementation contract
#   2. eth_sendRawTransaction ProxyAdmin.upgrade(targetProxy, newImpl) signed by owner
#   3. wait for the tx receipt
#   4. read the proxy EIP-1967 implementation slot
#      (0x360894a13ba1a3210667c828492db98dca3e2076cc3735a920a3ca505d382bbc)
#      via eth_getStorageAt and assert it == newImpl
#   5. (optional) call upgrade with a non-owner key and assert it reverts
# This needs the A8 devnet's funded sequencer key + deploy tooling.
log "SKIP-NOTE: full ProxyAdmin.upgrade() roundtrip is TODO-A8 (needs funded key)"

log "PASS (ownership check; upgrade roundtrip deferred to A8)"
