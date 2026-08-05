#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Integration test for the pure-Ethereum executor (executor_version=2):
#   1. build a single-node AIR chain (secp256k1 consensus key via build_chain.sh)
#   2. patch config.genesis for executor_version=2 + evmc revision + L2 alloc funding
#   3. start the node, wait for it to reach consensus
#   4. run an RPC test that reuses an EEST state-test fixture (sender secretKey,
#      recipient, value) to sign a simple legacy value-transfer and drive it
#      through eth_sendRawTransaction / eth_getTransactionReceipt / eth_getBalance
#
# Usage: bash tools/.ci/ci_check_eth_executor.sh [REPO_ROOT] [FIXTURE_DIR]
set -euo pipefail

REPO_ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
FIXTURE_DIR="${2:-${REPO_ROOT}/fixtures}"
BINARY="${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos"
BUILDER="${REPO_ROOT}/tools/BcosAirBuilder/build_chain.sh"
WORK_DIR="${REPO_ROOT}/tools/nodes/127.0.0.1/node0"
RPC_URL="http://127.0.0.1:8545"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
log()  { echo -e "${GREEN}[eth-executor-ci]${NC} $*"; }
fail() { echo -e "${RED}[eth-executor-ci] FAIL: $*${NC}" >&2; exit 1; }

stop_node() {
    if [ -f "${WORK_DIR}/stop.sh" ]; then
        (cd "$(dirname "${WORK_DIR}")" && bash stop_all.sh 2>/dev/null) || true
    fi
    pkill -9 -f "fisco-bcos" 2>/dev/null || true
    sleep 1
}
trap stop_node EXIT

[ -f "${BINARY}" ] || fail "binary not found: ${BINARY}"

# ---- 1. build the single-node chain ----
log "building single-node chain via build_chain.sh"
cd "${REPO_ROOT}/tools"
rm -rf nodes config
bash "${BUILDER}" -l "127.0.0.1:1" -e "${BINARY}" "" >/dev/null 2>&1 \
    || fail "build_chain.sh failed"
[ -f "${WORK_DIR}/config.genesis" ] || fail "config.genesis not generated"

# ---- 2. patch genesis for executor_version=2 ----
log "patching config.genesis for executor_version=2 + L2 alloc"
cd "${WORK_DIR}"
# executor version 1 -> 2
perl -p -i -e 's/version=1/version=2/' config.genesis
# evmc revision (cancun from genesis; the v2 executor needs an EVMC revision)
perl -p -i -e 's/^(\s*is_serial_execute=true)/$1\n    evm_revision=cancun/' config.genesis

# L2 mode is required so genesis [alloc] can pre-fund an EOA sender
# (the same mode the ethereum-executor targets). feature_raw_address stays off:
# it is mutually exclusive with the L2 MPT state root.
cat >> config.genesis <<'GENESIS_EOF'

[features]
    feature_l2_ethereum_compat=1
GENESIS_EOF

# Pre-fund the fixture sender (secretKey below) via genesis alloc. The alloc
# parser requires a `code` key; an empty value means "no code" (a plain EOA).
# Sender is from fixtures/state_tests/berlin/eip2930_access_list/test_transaction_intrinsic_gas_cost.json
cat >> config.genesis <<'GENESIS_EOF'
[alloc.0]
    address=0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0
    balance=1000000000000000000000
    nonce=0
    code=
GENESIS_EOF

# enable web3_rpc (eth_*) on port 8545
perl -p -i -e 'if (/\[web3_rpc\]/) { $f=1 } elsif ($f && s/enable\s*=\s*false/enable=true/) { $f=0 }' config.ini

# ---- 3. start node and wait for RPC ----
log "starting node"
bash start.sh >/dev/null 2>&1 || fail "start.sh failed"
for i in $(seq 1 30); do
    sleep 2
    if curl -s -X POST "${RPC_URL}" -H 'Content-Type: application/json' \
        -d '{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]}' 2>/dev/null | grep -q '"result"'; then
        log "RPC up after ~$((i*2))s"
        break
    fi
    [ "$i" -eq 30 ] && fail "node RPC did not come up"
done

# ---- 4. run the RPC simple-transfer test (reusing an EEST fixture) ----
log "running RPC value-transfer test"
python3 "${REPO_ROOT}/tools/.ci/eth_executor_rpc_test.py" \
    --rpc "${RPC_URL}" \
    --fixture-dir "${FIXTURE_DIR}" \
    --secret "0xa4e7ea6dc542e6de38bf1229c3ea744aa6b9f501386aa167ad42f49e3787066f" \
    --to "0xc3ea744aa6b9f501386aa167ad42f49e3787066f" \
    --sender "0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0" \
    || fail "eth_executor_rpc_test.py failed"

log "PASS: executor_version=2 integration test"
