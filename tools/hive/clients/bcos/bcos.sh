#!/usr/bin/env bash
# Copyright (c) FISCO-BCOS, Apache-2.0
#
# bcos.sh — hive client entrypoint for the FISCO-BCOS Ethereum L1 EL-mode node.
#
# Implements the hive eth1 client contract (https://github.com/ethereum/hive/blob/master/docs/clients.md):
#   1. translate the injected /genesis.json (geth format) into the node's INI
#      config.genesis (alloc + [eth_genesis_header] + [fork_timestamps])
#   2. map HIVE_* environment variables onto config.ini / node files
#   3. import /chain.rlp and /blocks/*.rlp before serving (best block = last
#      valid imported block)
#   4. exec the node; hive waits for TCP 8545
#
# Supported environment variables:
#   HIVE_CHAIN_ID     decimal chain id (informational; genesis.json is authoritative)
#   HIVE_NETWORK_ID   p2p network id (accepted, informational)
#   HIVE_LOGLEVEL     0-5, mapped onto the node log level
#   HIVE_BOOTNODE     enode URL(s) (space-separated) to dial via devp2p
#   HIVE_MINER        coinbase address (accepted; block production is Engine-API
#                     driven in EL mode, so this only sets the default beneficiary)
#   HIVE_NODETYPE     archive/full (accepted; the node is always archive)
#   HIVE_CHECK_LIVE_PORT  honoured implicitly: 8545 opens once the node is up
#
# Files:
#   /genesis.json  (mandatory) geth-format genesis
#   /chain.rlp     (optional)  concatenated RLP blocks to import before startup
#   /blocks/       (optional)  directory of *.rlp blocks, imported in name order
#   /jwtsecret     (optional)  Engine API JWT secret; presence enables 8551
set -u

# The EL executor's stackless-coroutine await chain accumulates one resume
# level per sequential storage write (no symmetric transfer on completion), so
# blocks touching hundreds of accounts (e.g. EIP-7702 many-delegation fixtures)
# overflow the default 8MiB main-thread stack under ASan. Lift the limit for
# the import and the node process alike. Must stay FINITE: "unlimited" breaks
# ASan's shadow-memory placement (sanitizers#856).
ulimit -s 2097152 2>/dev/null || true

echo "fisco-bcos hive client starting" >&2

cd /bcos

# ---------------------------------------------------------------- genesis ----
if [ ! -f /genesis.json ]; then
    echo "FATAL: /genesis.json not mounted (hive eth1 contract)" >&2
    exit 1
fi

# HIVE_* fork overrides take precedence over the chainConfig inside
# genesis.json (several smoke fixtures ship a config-less genesis and pass
# everything via the environment). Merge them in before conversion.
GENESIS_IN=/genesis.json
if env | grep -qE '^HIVE_(CHAIN_ID|FORK_|TERMINAL_TOTAL_DIFFICULTY|MERGE_BLOCK_ID|SHANGHAI_TIMESTAMP|CANCUN_TIMESTAMP|PRAGUE_TIMESTAMP|OSAKA_TIMESTAMP|BPO[12]_TIMESTAMP)'; then
    jq '
      .config = (.config // {}) |
      (if env.HIVE_CHAIN_ID                  then .config.chainId                = (env.HIVE_CHAIN_ID|tonumber)                  else . end) |
      (if env.HIVE_FORK_HOMESTEAD            then .config.homesteadBlock         = (env.HIVE_FORK_HOMESTEAD|tonumber)            else . end) |
      (if env.HIVE_FORK_DAO_BLOCK            then .config.daoForkBlock           = (env.HIVE_FORK_DAO_BLOCK|tonumber)            else . end) |
      (if env.HIVE_FORK_TANGERINE            then .config.eip150Block            = (env.HIVE_FORK_TANGERINE|tonumber)            else . end) |
      (if env.HIVE_FORK_SPURIOUS             then .config.eip155Block            = (env.HIVE_FORK_SPURIOUS|tonumber)             else . end) |
      (if env.HIVE_FORK_SPURIOUS             then .config.eip158Block            = (env.HIVE_FORK_SPURIOUS|tonumber)             else . end) |
      (if env.HIVE_FORK_BYZANTIUM            then .config.byzantiumBlock         = (env.HIVE_FORK_BYZANTIUM|tonumber)            else . end) |
      (if env.HIVE_FORK_CONSTANTINOPLE       then .config.constantinopleBlock    = (env.HIVE_FORK_CONSTANTINOPLE|tonumber)       else . end) |
      (if env.HIVE_FORK_PETERSBURG           then .config.petersburgBlock        = (env.HIVE_FORK_PETERSBURG|tonumber)           else . end) |
      (if env.HIVE_FORK_ISTANBUL             then .config.istanbulBlock          = (env.HIVE_FORK_ISTANBUL|tonumber)             else . end) |
      (if env.HIVE_FORK_MUIR_GLACIER         then .config.muirGlacierBlock       = (env.HIVE_FORK_MUIR_GLACIER|tonumber)         else . end) |
      (if env.HIVE_FORK_BERLIN               then .config.berlinBlock            = (env.HIVE_FORK_BERLIN|tonumber)               else . end) |
      (if env.HIVE_FORK_LONDON               then .config.londonBlock            = (env.HIVE_FORK_LONDON|tonumber)               else . end) |
      (if env.HIVE_FORK_ARROW_GLACIER        then .config.arrowGlacierBlock      = (env.HIVE_FORK_ARROW_GLACIER|tonumber)        else . end) |
      (if env.HIVE_FORK_GRAY_GLACIER         then .config.grayGlacierBlock       = (env.HIVE_FORK_GRAY_GLACIER|tonumber)         else . end) |
      (if env.HIVE_MERGE_BLOCK_ID            then .config.mergeNetsplitBlock     = (env.HIVE_MERGE_BLOCK_ID|tonumber)            else . end) |
      (if env.HIVE_TERMINAL_TOTAL_DIFFICULTY then .config.terminalTotalDifficulty = (env.HIVE_TERMINAL_TOTAL_DIFFICULTY|tonumber) else . end) |
      (if env.HIVE_SHANGHAI_TIMESTAMP        then .config.shanghaiTime           = (env.HIVE_SHANGHAI_TIMESTAMP|tonumber)        else . end) |
      (if env.HIVE_CANCUN_TIMESTAMP          then .config.cancunTime             = (env.HIVE_CANCUN_TIMESTAMP|tonumber)          else . end) |
      (if env.HIVE_PRAGUE_TIMESTAMP          then .config.pragueTime             = (env.HIVE_PRAGUE_TIMESTAMP|tonumber)          else . end) |
      (if env.HIVE_OSAKA_TIMESTAMP           then .config.osakaTime              = (env.HIVE_OSAKA_TIMESTAMP|tonumber)           else . end) |
      (if env.HIVE_BPO1_TIMESTAMP            then .config.bpo1Time               = (env.HIVE_BPO1_TIMESTAMP|tonumber)            else . end) |
      (if env.HIVE_BPO2_TIMESTAMP            then .config.bpo2Time               = (env.HIVE_BPO2_TIMESTAMP|tonumber)            else . end)
    ' /genesis.json > /bcos/genesis.patched.json
    GENESIS_IN=/bcos/genesis.patched.json
fi

if ! eth-sync-check --genesis2ini "${GENESIS_IN}" --output /bcos/config.genesis; then
    echo "FATAL: genesis.json -> config.genesis conversion failed" >&2
    exit 1
fi

CHAIN_ID=$(jq -r '.config.chainId // empty' "${GENESIS_IN}" 2>/dev/null || echo "")
CHAIN_ID=${CHAIN_ID:-1}

# ------------------------------------------------------------------- keys ----
# secp256k1 consensus identity (conf/node.pem) and gateway certs: generated
# per container — hive test networks are throwaway.
mkdir -p conf/engine
if [ ! -f conf/node.pem ]; then
    openssl ecparam -genkey -name secp256k1 -out conf/node.pem 2>/dev/null
    openssl ec -in conf/node.pem -pubout -outform DER 2>/dev/null \
        | tail -c 65 | tail -c +2 | xxd -p -c 64 > conf/node.nodeid
fi
if [ ! -f conf/ca.crt ]; then
    openssl req -x509 -newkey rsa:2048 -nodes -keyout conf/ca.key \
        -out conf/ca.crt -days 3650 -subj "/CN=fisco-bcos-hive" 2>/dev/null
    openssl req -new -newkey rsa:2048 -nodes -keyout conf/ssl.key \
        -out conf/ssl.csr -subj "/CN=node" 2>/dev/null
    openssl x509 -req -in conf/ssl.csr -CA conf/ca.crt -CAkey conf/ca.key \
        -CAcreateserial -out conf/ssl.crt -days 3650 2>/dev/null
fi

# devp2p node identity: stable per container; enode.sh derives the enode from it.
if [ ! -f node.rlpx.key ]; then
    openssl rand -hex 32 > node.rlpx.key
fi

# --------------------------------------------------------------- bootnodes ----
# validateNodeConfig requires a non-empty enode list; with no HIVE_BOOTNODE a
# syntactically valid but unreachable placeholder keeps the (autonomous) sync
# loop idle — connection failures are transient and only log WARNINGs.
if [ -n "${HIVE_BOOTNODE:-}" ]; then
    BOOTNODES=$(printf '%s' "${HIVE_BOOTNODE}" | tr ' ,' '\n\n' | grep -v '^$' \
        | jq -R . | jq -s .)
else
    # secp256k1 generator point as pubkey, port 1 = unreachable
    BOOTNODES='["enode://79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8@127.0.0.1:1"]'
fi
printf '%s\n' "${BOOTNODES}" > bootnodes.json

# ------------------------------------------------------------------ config ----
case "${HIVE_LOGLEVEL:-3}" in
    0) LOG_LEVEL=fatal ;;
    1) LOG_LEVEL=error ;;
    2) LOG_LEVEL=warning ;;
    3) LOG_LEVEL=info ;;
    4) LOG_LEVEL=debug ;;
    *) LOG_LEVEL=trace ;;
esac

# Engine API: enabled when the simulator mounted a JWT secret (engine suites).
ENGINE_ENABLED=false
if [ -f /jwtsecret ]; then
    tr -d '[:space:]' < /jwtsecret > conf/engine/jwt.hex
    ENGINE_ENABLED=true
fi

sed -e "s/__CHAIN_ID__/${CHAIN_ID}/g" \
    -e "s/__LOG_LEVEL__/${LOG_LEVEL}/g" \
    -e "s/__ENGINE_ENABLED__/${ENGINE_ENABLED}/g" \
    /bcos/config.ini.tpl > /bcos/config.ini

echo '{"nodes":[]}' > nodes.json

if [ -n "${HIVE_MINER:-}" ]; then
    echo "NOTE: HIVE_MINER=${HIVE_MINER} accepted; EL mode produces blocks via the Engine API only" >&2
fi
if [ -n "${HIVE_CLIQUE_PRIVATEKEY:-}" ]; then
    echo "WARN: clique PoA is not supported; HIVE_CLIQUE_* ignored" >&2
fi

# ----------------------------------------------------------------- import ----
# hive contract: import /chain.rlp first, then /blocks/*.rlp in name order;
# the best block after startup is the last VALID imported block (the importer
# skips invalid blocks and keeps going).
if [ -f /chain.rlp ]; then
    echo "importing /chain.rlp ..." >&2
    fisco-bcos -c config.ini -g config.genesis --import-blocks /chain.rlp
fi
if [ -d /blocks ] && [ -n "$(ls -A /blocks 2>/dev/null)" ]; then
    echo "importing /blocks ..." >&2
    fisco-bcos -c config.ini -g config.genesis --import-blocks /blocks
fi

# ------------------------------------------------------------------- exec ----
echo "starting fisco-bcos (chain id ${CHAIN_ID}, engine api: ${ENGINE_ENABLED})" >&2
exec fisco-bcos -c config.ini -g config.genesis
