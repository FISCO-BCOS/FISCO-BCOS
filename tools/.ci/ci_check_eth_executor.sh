#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# Integration test for the pure-Ethereum executor (executor_version=2):
#   1. build a single-node AIR chain (secp256k1 consensus key via build_chain.sh)
#   2. patch config.genesis for executor_version=2 + [fork_timestamps] + L2 alloc funding
#   3. start the node under the built-in single-node driver (config.ini [debug] enable;
#      the Engine driver needs an external consensus client)
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
fail() {
    # Dump the node log so a CI failure is diagnosable: the node is started with
    # output to /dev/null, so without this a failure produces one FAIL: line and
    # nothing from the node.
    echo -e "${RED}[eth-executor-ci] FAIL: $*${NC}" >&2
    echo -e "${YELLOW}[eth-executor-ci] last node log lines:${NC}" >&2
    tail -n 200 "${WORK_DIR}"/log/*.log 2>/dev/null || true
    exit 1
}

# PID of the node we started (captured after start.sh), so the EXIT trap stops
# exactly this process instead of every fisco-bcos on the machine.
NODE_PID=""
stop_node() {
    if [ -n "${NODE_PID}" ] && kill -0 "${NODE_PID}" 2>/dev/null; then
        kill -9 "${NODE_PID}" 2>/dev/null || true
    fi
    if [ -f "${WORK_DIR}/stop.sh" ]; then
        (cd "$(dirname "${WORK_DIR}")" && bash stop_all.sh 2>/dev/null) || true
    fi
    sleep 1
}
trap stop_node EXIT

[ -f "${BINARY}" ] || fail "binary not found: ${BINARY}"

# ---- 1. build the single-node chain ----
log "building single-node chain via build_chain.sh"
cd "${REPO_ROOT}/tools"
rm -rf nodes config
# -v 3.18.0 pins compatibility_version (build_chain.sh defaults to v3.16.0). Without it
# Ledger::buildGenesisBlock gates the evmc_revision write on >= V3_18_0 and the config
# set below would never reach on-chain state — the test would silently run the binary's
# default revision instead of cancun while still passing.
bash "${BUILDER}" -l "127.0.0.1:1" -v 3.18.0 -e "${BINARY}" "" >/dev/null 2>&1 \
    || fail "build_chain.sh failed"
[ -f "${WORK_DIR}/config.genesis" ] || fail "config.genesis not generated"

# ---- 2. patch genesis for executor_version=2 ----
log "patching config.genesis for executor_version=2 + L2 alloc"
cd "${WORK_DIR}"
# executor version 1 -> 2
perl -p -i -e 's/version=1/version=2/' config.genesis
# executor version 2 takes its chain id from [chain] chain_id (a decimal), not [web3]
perl -p -i -e 's/chain_id=chain0/chain_id=20200/' config.genesis
# The v2 fork schedule: the block driver derives the EVM revision and the header rules per
# block timestamp from it. Cancun from genesis; Prague stays in the future (a Prague block
# runs the EIP-7002/7251 system calls, whose contracts this single-alloc genesis does not
# carry).
cat >> config.genesis <<'GENESIS_EOF'

[fork_timestamps]
london_time=0
paris_time=0
shanghai_time=0
cancun_time=0
prague_time=4102444800
merge_block=0
GENESIS_EOF
# The RPC dry-run path (eth_call / eth_estimateGas) still reads the EVM revision from the
# on-chain evmc_revision row, which only evm_revision writes; keep it, matching the schedule.
perl -p -i -e 's/^(\s*is_serial_execute=true)/$1\n    evm_revision=cancun/' config.genesis

# executor_version=2 (patched above) already puts the chain on the Ethereum lane, so
# genesis [alloc] can pre-fund an EOA sender (the same lane the ethereum-executor
# targets). The Eth lane is mode-aware: a fresh
# chain is born with the binary account-table layout ("/s/<20 raw bytes>") and the lane
# reads and writes it through account::ethLaneAccountTableName.

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

# The Ethereum lane (executor_version >= 2) now REQUIRES an [eth_genesis_header]
# section (NodeConfig::validateL2Invariants, upstream #5420): an Ethereum-lane chain
# without it would mint a Tars-hashed B0 that no op-node/op-reth can match.
# The section is a full Prague-era B0 header artifact; the node cross-checks
# state_root against the MPT root it derives from the [alloc.*] sections and
# hash against keccak256(rlp(header)) of the other 21 fields.
#
# Values below are the standard empty-chain defaults from
# the harness's tools/opstack-genesis/gen_eth_header_fixture.py, EXCEPT:
#   state_root = 0x6ea0c8bc... — the genesis MPT root for the single alloc
#     above, computed with the node's own Ledger::computeGenesisStateTrie;
#     MUST be regenerated if [alloc.0] changes (a stale root fails genesis).
#   hash       = 0xf66beef7... — keccak256(rlp(header)) over the 21 fields,
#     from gen_eth_header_fixture.py with the state_root override above.
cat >> config.genesis <<'GENESIS_EOF'

[eth_genesis_header]
parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000
sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347
miner=0x4200000000000000000000000000000000000011
state_root=0x6ea0c8bc0a9b451c2bab98df63d4be058d3b82b9b3a2e5e02cbf0122ef95e5a5
transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
logs_bloom=0x00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
difficulty=0x0
number=0x0
gas_limit=0x1c9c380
gas_used=0x0
timestamp=0x689d5c00
extra_data=0x01000000fa000000060000000000000000
mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000
nonce=0x0000000000000000
base_fee_per_gas=0x3b9aca00
withdrawals_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
blob_gas_used=0x0
excess_blob_gas=0x0
parent_beacon_block_root=0x0000000000000000000000000000000000000000000000000000000000000000
requests_hash=0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
hash=0xf66beef79256c0b3f1fd2852040413c5dffa1ff44f0f78421ae17cf8f397a1a5
GENESIS_EOF

# enable web3_rpc (eth_*) on port 8545
perl -p -i -e 'if (/\[web3_rpc\]/) { $f=1 } elsif ($f && s/enable\s*=\s*false/enable=true/) { $f=0 }' config.ini
# executor_version=2 is driven over the Engine API unless the node produces its own blocks:
# select the built-in single-node driver ([debug] is a hidden section no template carries)
grep -q '^\[debug\]' config.ini && fail "config.ini already has a [debug] section"
printf '\n[debug]\n    enable=true\n' >> config.ini

# ---- 3. start node and wait for RPC ----
log "starting node"
bash start.sh >/dev/null 2>&1 || fail "start.sh failed"
# Capture the node PID (scoped to a fisco-bcos launched with config.ini) for the
# EXIT trap; on an isolated CI runner this is the only match.
NODE_PID=$(pgrep -f "fisco-bcos.*-c config.ini" || true)
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
# --require-fixture: in CI the "Download EEST fixtures" step runs first, so a
# missing fixture is a real failure (the test's EEST-reuse claim must not
# silently disappear). Local runs without fixtures omit the flag.
python3 "${REPO_ROOT}/tools/.ci/eth_executor_rpc_test.py" \
    --rpc "${RPC_URL}" \
    --fixture-dir "${FIXTURE_DIR}" \
    --require-fixture \
    --secret "0xa4e7ea6dc542e6de38bf1229c3ea744aa6b9f501386aa167ad42f49e3787066f" \
    --to "0xc3ea744aa6b9f501386aa167ad42f49e3787066f" \
    --sender "0x9015bca99e8d49107c33b2cac14013a8dfd2c1b0" \
    || fail "eth_executor_rpc_test.py failed"

# ---- 5. pin the fork the blocks are built under ----
# Without this the test can never go red on a fork-schedule regression: a value transfer
# executes the same under every fork. The block driver derives the header era from
# [fork_timestamps], so block 1 must carry the Cancun header fields (blob gas, beacon root)
# and no Prague requestsHash.
log "pinning the block header era (expect cancun)"
python3 - "${RPC_URL}" <<'PYEOF' || fail "block 1 is not a Cancun-era header"
import json, sys, urllib.request
req = urllib.request.Request(sys.argv[1], json.dumps({"jsonrpc": "2.0", "id": 1,
    "method": "eth_getBlockByNumber", "params": ["0x1", False]}).encode(),
    {"Content-Type": "application/json"})
block = json.load(urllib.request.urlopen(req))["result"]
for field in ("blobGasUsed", "excessBlobGas", "parentBeaconBlockRoot", "withdrawalsRoot"):
    assert field in block, f"block 1 lacks {field}: {block}"
assert "requestsHash" not in block, f"block 1 carries the Prague requestsHash: {block}"
print("block 1 header era: cancun")
PYEOF

log "PASS: executor_version=2 integration test"
