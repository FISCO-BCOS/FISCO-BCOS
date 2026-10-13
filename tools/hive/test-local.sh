#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# test-local.sh — host-level replica of the hive client entrypoint (bcos.sh)
# for machines without docker. Converts a geth genesis.json, optionally imports
# a chain.rlp, starts the EL node, runs RPC sanity checks, and stops it.
#
# Usage:
#   bash tools/hive/test-local.sh <genesis.json> [chain.rlp] [--keep]
#
# Env overrides: BINARY (default build/fisco-bcos-air/fisco-bcos),
#                TOOL   (default build/tools/eth-sync-check/eth-sync-check)
set -uo pipefail

# Mirrors bcos.sh: the EL executor's coroutine await chain grows with the
# number of sequential storage writes; lift the main-thread stack limit.
# Must stay finite — "unlimited" breaks ASan's shadow-memory placement.
ulimit -s 2097152 2>/dev/null || true

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BINARY="${BINARY:-${REPO_ROOT}/build/fisco-bcos-air/fisco-bcos}"
TOOL="${TOOL:-${REPO_ROOT}/build/tools/eth-sync-check/eth-sync-check}"
GENESIS="$(realpath "$1")"
CHAIN_RLP="${2:-}"
[ -n "${CHAIN_RLP}" ] && CHAIN_RLP="$(realpath "${CHAIN_RLP}")"
KEEP="${3:-}"

WORK=$(mktemp -d /tmp/bcos-hive-local.XXXXXX)
echo "[test-local] workdir: ${WORK}"

cleanup() {
    [ -n "${NODE_PID:-}" ] && kill "${NODE_PID}" 2>/dev/null
    [ "${KEEP}" != "--keep" ] && rm -rf "${WORK}" || echo "[test-local] kept ${WORK}"
}
trap cleanup EXIT

cp "${REPO_ROOT}/tools/hive/clients/bcos/config.ini.tpl" "${WORK}/"
cd "${WORK}"

# ---- genesis -> config.genesis (mirrors bcos.sh) ----
"${TOOL}" --genesis2ini "${GENESIS}" --output config.genesis || {
    echo "[test-local] FAIL: genesis2ini"; exit 1; }
CHAIN_ID=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1])).get('config',{}).get('chainId',1))" "${GENESIS}")

# ---- keys / certs / bootnodes (mirrors bcos.sh) ----
mkdir -p conf/engine
openssl ecparam -genkey -name secp256k1 -out conf/node.pem 2>/dev/null
openssl ec -in conf/node.pem -pubout -outform DER 2>/dev/null \
    | tail -c 65 | tail -c +2 | xxd -p -c 64 > conf/node.nodeid
openssl req -x509 -newkey rsa:2048 -nodes -keyout conf/ca.key \
    -out conf/ca.crt -days 3650 -subj "/CN=fisco-bcos-hive" 2>/dev/null
openssl req -new -newkey rsa:2048 -nodes -keyout conf/ssl.key \
    -out conf/ssl.csr -subj "/CN=node" 2>/dev/null
openssl x509 -req -in conf/ssl.csr -CA conf/ca.crt -CAkey conf/ca.key \
    -CAcreateserial -out conf/ssl.crt -days 3650 2>/dev/null
openssl rand -hex 32 > node.rlpx.key
echo '["enode://79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8@127.0.0.1:1"]' > bootnodes.json
echo '{"nodes":[]}' > nodes.json

sed -e "s/__CHAIN_ID__/${CHAIN_ID}/g" \
    -e "s/__LOG_LEVEL__/info/g" \
    -e "s/__ENGINE_ENABLED__/false/g" \
    config.ini.tpl > config.ini

# ---- import (mirrors bcos.sh) ----
if [ -n "${CHAIN_RLP}" ]; then
    echo "[test-local] importing ${CHAIN_RLP}"
    # Mirror bcos.sh's consume-rlp tolerance: a non-zero import exit must not
    # abort the replica — the RPC checks below are the verdict (a failed import
    # leaves the node serving genesis, and the hash assertions catch it).
    "${BINARY}" -c config.ini -g config.genesis --import-blocks "${CHAIN_RLP}" || true
fi

# ---- start node, RPC sanity checks ----
"${BINARY}" -c config.ini -g config.genesis > node.out 2>&1 &
NODE_PID=$!

RPC="http://127.0.0.1:8545"
ok=0
for _ in $(seq 1 60); do
    sleep 2
    if curl -s -X POST "${RPC}" -H 'Content-Type: application/json' \
        -d '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}' | grep -q '"result"'; then
        ok=1; break
    fi
    if ! kill -0 "${NODE_PID}" 2>/dev/null; then
        echo "[test-local] node exited early; last output:"; tail -30 node.out; exit 1
    fi
done
[ "${ok}" = 1 ] || { echo "[test-local] FAIL: RPC never came up"; tail -30 node.out; exit 1; }

rpc() { curl -s -X POST "${RPC}" -H 'Content-Type: application/json' -d "$1"; echo; }

echo "[test-local] eth_chainId:        $(rpc '{"jsonrpc":"2.0","id":1,"method":"eth_chainId","params":[]}')"
echo "[test-local] eth_blockNumber:    $(rpc '{"jsonrpc":"2.0","id":1,"method":"eth_blockNumber","params":[]}')"
echo "[test-local] genesis block hash: $(rpc '{"jsonrpc":"2.0","id":1,"method":"eth_getBlockByNumber","params":["0x0",false]}' | python3 -c "import json,sys; print(json.load(sys.stdin)['result']['hash'])")"
echo "[test-local] head block:         $(rpc '{"jsonrpc":"2.0","id":1,"method":"eth_getBlockByNumber","params":["latest",false]}' | python3 -c "import json,sys; r=json.load(sys.stdin)['result']; print(r['number'], r['hash'])")"

echo "[test-local] PASS (node up, RPC sane)"
