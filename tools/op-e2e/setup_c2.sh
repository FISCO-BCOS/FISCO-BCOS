#!/usr/bin/env bash
# setup_c2.sh — C2 devnet 一键搭建:anvil L1 + op-deployer 部署 + FISCO L2 + op-node sequencer
#
# 串起完整链路(op-deployer terminal allocs → FISCO overlay → config.genesis →
# FISCO 启动 → op-node 出块)。幂等:已完成的步骤跳过(标志文件)。
#
# 依赖:anvil(foundry) / op-deployer(monorepo 构建) / op-node(monorepo 构建) /
#       python3(pyyaml) / forge(bcos-l2-contracts 编译) / openssl
#
# 用法:
#   bash setup_c2.sh                     # 全链路
#   bash setup_c2.sh -s 2                # 从第 2 步开始(跳过已完成步骤)
#   C2=~/c2 bash setup_c2.sh             # 自定义工作区(默认 /tmp/c2)
#
# 独立目录隔离实例(多会话共用一台机时,避免互踩 /tmp/c2):
#   C2=/tmp/c2b ANVIL_PORT=8649 FISCO_WEB3=8655 FISCO_ENGINE=8666 \
#   FISCO_RPC=21213 FISCO_P2P=32400 OP_NODE_PORT=9645 OP_BATCHER_PORT=8647 \
#   OP_NODE_EXTRA_FLAGS="--p2p.disable" bash setup_c2.sh
# (端口全部可 env 覆盖,默认值不变;隔离实例务必 --p2p.disable,防止两个
#  devnet 通过 gossip 互联。) 配套脚本 withdraw_claim.py / withdraw_e2e.sh
# 用 C2_L1 / C2_L2_WEB3 / C2_OP_NODE / C2_STATE 指向同一实例。
#
# 关键配置(踩坑后固化,见 docs/2026-08-19-session-handoff-final.md):
#   - anvil L1: 8549,chain 900900,块时间 12s;op-deployer live 部署 L1 合约
#   - FISCO L2: 8555(web3) / 8566(engine),chain 914901
#   - rollup.json 的 L1 genesis 必须 = anvil block 0 哈希(非部署块)
#   - rollup.json 的 L2 genesis 必须 = FISCO 实际创世哈希(非 op-deployer 计算值)
#   - eth_genesis_header.timestamp 用秒(L1 时间戳;C++ 侧 ×1000 存内部毫秒)
#   - [features] feature_op_jovian=true(否则 9B extraData,op-node 拒绝)
#   - op-node: --l1.beacon.ignore + --rollup.l1-chain-config(anvil 需 cancunTime)
set -eu

# ---------- 可调参数 ----------
C2="${C2:-/tmp/c2}"
MONOREPO="${MONOREPO:-/Users/octopus/octo/code/blockchain-impl/optimism}"
FISCO_BIN="${FISCO_BIN:-/Users/octopus/octo/code/FISCO-BCOS/build/fisco-bcos-air/fisco-bcos}"
OPGEN="${OPGEN:-/Users/octopus/octo/code/FISCO-BCOS/tools/opstack-genesis}"
L2CONTRACTS="${L2CONTRACTS:-/Users/octopus/octo/code/FISCO-BCOS/bcos-l2-contracts}"

# 端口(可 env 覆盖——隔离实例用: 见文件头"独立目录"说明; 默认值不变)
ANVIL_PORT="${ANVIL_PORT:-8549}"; ANVIL_CHAIN="${ANVIL_CHAIN:-900900}"
FISCO_WEB3="${FISCO_WEB3:-8555}"; FISCO_ENGINE="${FISCO_ENGINE:-8566}"
FISCO_RPC="${FISCO_RPC:-20213}"; FISCO_P2P="${FISCO_P2P:-31400}"
L2_CHAIN="${L2_CHAIN:-914901}"
OP_NODE_PORT="${OP_NODE_PORT:-9545}"
OP_BATCHER_PORT="${OP_BATCHER_PORT:-8547}"
# Appended verbatim to the op-node command line (isolated instances want
# "--p2p.disable" so two devnets never gossip blocks at each other).
OP_NODE_EXTRA_FLAGS="${OP_NODE_EXTRA_FLAGS:-}"
# op-node refreshes its L1 safe/finalized VIEW on this cadence. The upstream
# default (6m24s) is tuned for production L1 epochs; on a devnet it means the
# FIRST finalized update lands up to 384s after start and finalized_l2 then
# advances in 384s jumps — the dominant cost of every withdrawal e2e wait
# (measured: finalized_l2 == safe_l2 within seconds at 2s polling; anvil's
# finalized tag itself tracks head-2 with --slots-in-an-epoch 1, so this poll
# was the only thing holding it back). Devnet launcher default; production
# nodes keep the upstream default.
OP_NODE_EPOCH_POLL="${OP_NODE_EPOCH_POLL:-5s}"

# Dispute/DA clocks — devnet defaults are the FAST profile (a full withdrawal
# claim round in ~2-3 minutes); slow them per-instance via env if a test needs
# longer horizons. Keep the InvalidClockExtension invariant above when
# compressing further: max(ext*2, ext+PREIMAGE_CHALLENGE_SECONDS) <=
# FAULT_GAME_MAX_CLOCK.
ANVIL_BLOCK_TIME="${ANVIL_BLOCK_TIME:-2}"
PROOF_MATURITY_SECONDS="${PROOF_MATURITY_SECONDS:-12}"
DISPUTE_FINALITY_SECONDS="${DISPUTE_FINALITY_SECONDS:-6}"
FAULT_GAME_MAX_CLOCK="${FAULT_GAME_MAX_CLOCK:-45}"
PREIMAGE_CHALLENGE_SECONDS="${PREIMAGE_CHALLENGE_SECONDS:-30}"
# DelayedWETH unlock delay (immutable in the deployed implementation): the bond
# recovery is a TWO-STEP claimCredit — the first unlocks (starts this countdown),
# the second pays the bond out. Standard default is 302400s (3.5 days), which
# puts recovery outside any e2e window; 10s keeps it testable end-to-end.
WETH_UNLOCK_SECONDS="${WETH_UNLOCK_SECONDS:-10}"
BATCHER_MAX_CHANNEL="${BATCHER_MAX_CHANNEL:-1}"

# 账户(anvil 标准助记词)
DEV0=0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80   # deployer/owner
DEV1=0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d   # signer/proposer
AUTH_ADMIN=0x70997970C51812dc3A010C7d01b50e0d17dc79C8                     # governance owner

START="${START:-1}"; END="${END:-7}"
step() { echo; echo "==== [$1/7] $2 ===="; }
log() { echo "  >> $*"; }
die() { echo "  !! $*" >&2; exit 1; }
step_run() { [ "$1" -ge "$START" ] && [ "$1" -le "$END" ]; }

mkdir -p "$C2/fisco/conf"
cd "$C2"

# ---------- 1. anvil L1 ----------
if step_run 1; then
  step 1 "anvil L1 (chain $ANVIL_CHAIN, port $ANVIL_PORT)"
  if curl -s -o /dev/null -X POST http://127.0.0.1:$ANVIL_PORT \
       -H 'Content-Type: application/json' \
       -d "{\"jsonrpc\":\"2.0\",\"method\":\"eth_chainId\",\"params\":[],\"id\":1}"; then
    log "anvil 已在运行,跳过"
  else
    # --slots-in-an-epoch 1: anvil's finalized tag = head - 2*slots (default 32
    # slots = 64 blocks = 12.8min finality lag at 12s blocks); with 1 slot the
    # lag is 2 blocks (~24s), so op-node finalized_l2 tracks the head closely
    # and withdrawal claims don't wait ~20min for finality.
    anvil --port $ANVIL_PORT --chain-id $ANVIL_CHAIN \
      --mnemonic "test test test test test test test test test test test junk" \
      --block-time "$ANVIL_BLOCK_TIME" \
      --slots-in-an-epoch "${ANVIL_SLOTS_IN_AN_EPOCH:-1}" \
      > "$C2/anvil.log" 2>&1 &
    echo $! > "$C2/anvil.pid"
    sleep 2
    log "anvil 启动(PID $(cat "$C2/anvil.pid"))"
  fi
fi

# ---------- 2. op-deployer live 部署 ----------
if step_run 2; then
  step 2 "op-deployer 部署 L1 合约(live)"
  [ -x "$C2/op-deployer" ] || die "op-deployer 不存在: $C2/op-deployer(需 monorepo 构建)"
  rm -f "$C2/state.json"
  "$C2/op-deployer" init --l1-chain-id $ANVIL_CHAIN --l2-chain-ids $L2_CHAIN \
    --workdir "$C2" --intent-type custom || die "op-deployer init 失败"
  cat > "$C2/intent.toml" <<EOF
configType = "custom"
opDeployerVersion = "v0.0.0-dev"
l1ChainID = $ANVIL_CHAIN
fundDevAccounts = true
l1ContractsLocator = "embedded"
l2ContractsLocator = "embedded"
useInterop = false

[superchainRoles]
  SuperchainProxyAdminOwner = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  SuperchainGuardian = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  Challenger = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"

[[chains]]
  id = "0x00000000000000000000000000000000000000000000000000000000000df5d5"
  baseFeeVaultRecipient = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  l1FeeVaultRecipient = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  sequencerFeeVaultRecipient = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  operatorFeeVaultRecipient = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
  eip1559DenominatorCanyon = 250
  eip1559Denominator = 8
  eip1559Elasticity = 2
  gasLimit = 30000000
  operatorFeeScalar = 0
  operatorFeeConstant = 0
  minBaseFee = 1000000000
  daFootprintGasScalar = 312
  [chains.roles]
    l1ProxyAdminOwner = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
    l2ProxyAdminOwner = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
    systemConfigOwner = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
    unsafeBlockSigner = "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266"
    batcher = "0x3C44CdDdB6a900fa2b585dd299e03d12FA4293bc"
    proposer = "0x70997970C51812dc3A010C7d01b50e0d17dc79C8"
    # The dispute game challenger role MUST differ from the proposer: game type 1
    # is a PermissionedDisputeGame — only (proposer, challenger) may move — and
    # the adversarial e2e scenarios (withdraw_claim --contest) need a real
    # counterparty (DEV0) rather than the proposer attacking its own claims.
    challenger = "${INTENT_CHALLENGER:-0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266}"

# Devnet-only compressed dispute timelines (op-e2e parity: op-e2e/config/init.go).
# The whole withdrawal claim flow (prove -> resolveClaim -> resolve -> finalize)
# then runs in REAL time in ~2 minutes. NEVER warp the L1 clock instead: a warped
# L1 clock puts the sequencer into permanent noTxPool catch-up mode ~30 minutes
# later (docs/2026-08-23-session-handoff-final.md 裁决 8) — unrecoverable.
#
# Keys are the JSON tags of op-deployer's SuperchainProofParams / ChainProofParams;
# globalDeployOverrides feeds both merge sites (pipeline/implementations.go for the
# portal params, pipeline/opchain.go for the per-chain game params).
#
# Invariant (FaultDisputeGame.sol, InvalidClockExtension 0x8d77ecac):
#   max(clockExtension*2, clockExtension + preimageOracleChallengePeriod)
#     <= faultGameMaxClockDuration
# so the preimage challenge period MUST be compressed together with the clock —
# the standard default is 86400s and leaves room for no compression at all.
# Uncontested resolveClaim(0) unlocks after ~maxClockDuration/2 (~45s here).
[globalDeployOverrides]
proofMaturityDelaySeconds = $PROOF_MATURITY_SECONDS
disputeGameFinalityDelaySeconds = $DISPUTE_FINALITY_SECONDS
preimageOracleChallengePeriod = $PREIMAGE_CHALLENGE_SECONDS
faultGameClockExtension = 1
faultGameMaxClockDuration = $FAULT_GAME_MAX_CLOCK
faultGameWithdrawalDelay = $WETH_UNLOCK_SECONDS
dangerouslyAllowCustomDisputeParameters = true
EOF
  "$C2/op-deployer" --log.level info apply \
    --l1-rpc-url http://127.0.0.1:$ANVIL_PORT \
    --private-key $DEV0 \
    --workdir "$C2" || die "op-deployer apply 失败"
  "$C2/op-deployer" inspect rollup $L2_CHAIN --workdir "$C2" > "$C2/rollup.json" 2>/dev/null || \
    die "inspect rollup 失败"
  # ── 根因 F 固化（2026-08-23 C2 重建踩坑重现）────────────────────────────
  # op-deployer custom-intent 部署后 L1 SystemConfig.eip1559Params 为全零
  # （intent.toml 的 eip1559Denominator=8 并不会落到链上）。零参数下 op-node 的
  # FCU attrs 携带 0x0，引擎按 Holocene 语义编码出零 denominator 的 extraData，
  # op-node 立即拒绝（"extraData must encode a non-zero eip-1559 denominator"），
  # 块生产完全卡死；且零参数费率数学会让引擎的状态写入产生每次执行不同的垃圾
  # 状态（表现为 stateRoot 六路比对随机失败——曾误诊为 ①a 增量 MPT 的编码 bug，
  # 参数修复后增量/全量根完全一致、①a 节点落盘正常）。修复 = 部署后立即用
  # owner(DEV0) 设置 8/2，并把 rollup.json 的 genesis.system_config.eip1559Params
  # 同步为非零值（op-node 启动时校验）。
  SYSTEM_CONFIG=$(python3 -c "import json; print(json.load(open('$C2/rollup.json'))['l1_system_config_address'])" 2>/dev/null) || true
  if [ -n "${SYSTEM_CONFIG:-}" ]; then
    cast send "$SYSTEM_CONFIG" "setEIP1559Params(uint32,uint32)" 8 2 \
      --rpc-url http://127.0.0.1:$ANVIL_PORT \
      --private-key $DEV0 > /dev/null || die "setEIP1559Params(8,2) 失败"
    log "SystemConfig eip1559Params 已设为 8/2（根因 F 修复）"
  fi
  # 兜底：若 inspect 未反映链上最新值，强制同步（op-node 启动时校验该字段非零）
  python3 - "$C2/rollup.json" <<'PYEOF'
import json, sys
path = sys.argv[1]
r = json.load(open(path))
r['genesis']['system_config']['eip1559Params'] = '0x0000000800000002'
json.dump(r, open(path, 'w'), indent=2)
PYEOF
  "$C2/op-deployer" inspect genesis $L2_CHAIN --workdir "$C2" > "$C2/l2genesis.json" 2>/dev/null || \
    die "inspect genesis 失败"
  log "rollup.json + l2genesis.json 就绪"
fi

# ---------- 3. allocs + eth_genesis_header ----------
if step_run 3; then
  step 3 "allocs.ini + eth_genesis_header"
  python3 -c "import yaml" 2>/dev/null || die "pyyaml 未安装"
  NEW_HASH=$(sha256sum "$C2/l2genesis.json" | awk '{print $1}')
  sed -i '' "s/base_allocs_sha256:.*/base_allocs_sha256: \"$NEW_HASH\"/" "$OPGEN/chain-config-c2.yaml"
  python3 "$OPGEN/build-allocs.py" \
    --config "$OPGEN/chain-config-c2.yaml" \
    --contracts "$L2CONTRACTS" \
    --base-allocs "$C2/l2genesis.json" \
    --out "$C2/allocs-new.ini" || die "build-allocs 失败"
  log "$(grep -c '^\[alloc' "$C2/allocs-new.ini") 个 alloc"

  # L1 时间戳(秒)→ header 用毫秒(FISCO 内部毫秒存储)
  L1_TS=$(python3 -c "
import json, urllib.request
req = urllib.request.Request('http://127.0.0.1:$ANVIL_PORT',
    data=b'{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false],\"id\":1}',
    headers={'Content-Type':'application/json'})
d = json.loads(urllib.request.urlopen(req, timeout=10).read())
print(int(d['result']['timestamp'], 16))
")
  # artifact 时间戳必须是秒(Ethereum header 域);C++ NodeConfig→applyEthGenesisHeader
  # 自己 ×1000 存内部毫秒。此处再 ×1000 会让 RPC 返回毫秒,op-node 排程序等待
  # 时间戳到 year-58k(getPayload 永不触发)——见 2026-08-22 交接文档问题 E。
  python3 -c "
import json
json.dump({'timestamp': hex($L1_TS)}, open('$C2/header_override.json', 'w'))
"
  python3 "$OPGEN/gen_eth_header_fixture.py" --toml --allocs "$C2/allocs-new.ini" \
    "$C2/header_override.json" > "$C2/eth_genesis_header.ini" || die "gen_eth_header 失败"
  log "eth_genesis_header 就绪(state_root=$(grep '^state_root=' "$C2/eth_genesis_header.ini" | cut -d= -f2 | head -c 18)...)"
fi

# ---------- 4. FISCO config.genesis + 启动 ----------
if step_run 4; then
  step 4 "FISCO L2 装配 + 启动"
  [ -x "$FISCO_BIN" ] || die "FISCO 二进制不存在: $FISCO_BIN"
  # 节点密钥/证书(幂等:已存在跳过)
  OPENSSL=$(command -v openssl)
  [ -f "$C2/fisco/node.pem" ] || "$OPENSSL" ecparam -genkey -name secp256k1 -noout -out "$C2/fisco/node.pem"
  NODE_ID=$("$OPENSSL" ec -in "$C2/fisco/node.pem" -pubout -conv_form uncompressed 2>/dev/null \
    | grep -v "PUBLIC KEY" | tr -d '\n' | base64 -d | tail -c 64 | xxd -p -c 64 | tr -d '\n')
  if [ ! -f "$C2/fisco/conf/ssl.crt" ]; then
    ( cd "$C2/fisco/conf" && \
      "$OPENSSL" genrsa -out ca.key 2048 2>/dev/null && \
      "$OPENSSL" req -new -x509 -days 3650 -subj "/CN=c2-ca/O=fisco-bcos/OU=chain" \
        -key ca.key -out ca.crt 2>/dev/null && \
      "$OPENSSL" genrsa -out ssl.key 2048 2>/dev/null && \
      "$OPENSSL" req -new -sha256 -subj "/CN=c2-node/O=fisco-bcos/OU=agency" \
        -key ssl.key -out node.csr 2>/dev/null && \
      "$OPENSSL" x509 -req -days 3650 -in node.csr -CA ca.crt -CAkey ca.key \
        -CAcreateserial -sha256 -out ssl.crt 2>/dev/null ) || die "证书生成失败"
  fi
  [ -f "$C2/fisco/conf/tars_proxy.ini" ] || echo "# in-process services" > "$C2/fisco/conf/tars_proxy.ini"
  printf '{"nodes":["127.0.0.1:%d"]}' $FISCO_P2P > "$C2/fisco/nodes.json"
  [ -f "$C2/fisco/jwt.hex" ] || "$OPENSSL" rand -hex 32 > "$C2/fisco/jwt.hex"

  cat > "$C2/fisco/config.genesis" <<EOF
[chain]
    sm_crypto=false
    chain_id=$L2_CHAIN
    group_id=1
    isthmus_time=0
    jovian_time=0
[consensus]
    consensus_type=pbft
    block_tx_count_limit=1000
    leader_period=1
    node.0=$NODE_ID
    enable_single_node_consensus=false
    block_interval=1000
    produce_empty_blocks=false
    fee_recipient=0x4200000000000000000000000000000000000011
[version]
    compatibility_version=3.18.0
[tx]
    gas_limit=3000000000
[executor]
    is_auth_check=false
    is_serial_execute=true
    version=3
    evm_revision_forks=0:prague
    auth_admin_account=$AUTH_ADMIN
[features]
    feature_l2_ethereum_compat=true
    feature_op_jovian=true
$(cat "$C2/eth_genesis_header.ini")
[web3]
    chain_id=$L2_CHAIN
[service]
    without_tars_framework=true
[rpc]
    listen_ip=127.0.0.1
    listen_port=$FISCO_RPC
[web3_rpc]
    enable=true
    listen_ip=127.0.0.1
    listen_port=$FISCO_WEB3
[op_engine_rpc]
    enable=true
    listen_ip=127.0.0.1
    listen_port=$FISCO_ENGINE
    jwt_secret_file=jwt.hex
[storage]
    enable_cache=true
[p2p]
    listen_ip=0.0.0.0
    listen_port=$FISCO_P2P
    nodes_path=./
    nodes_file=nodes.json
[cert]
    ca_path=./conf
    ca_cert=ca.crt
    node_cert=ssl.crt
    node_key=ssl.key
[security]
    private_key_path=node.pem
[log]
    enable=true
    log_path=./log
    level=info
$(cat "$C2/allocs-new.ini")
EOF
  log "config.genesis 就绪($(wc -l < "$C2/fisco/config.genesis") 行)"

  # 启动 FISCO(ulimit 解除 RocksDB 栈溢出)
  kill "$(cat "$C2/fisco/node.pid" 2>/dev/null)" 2>/dev/null || true
  sleep 1
  ulimit -s 65520
  pushd "$C2/fisco" > /dev/null
  nohup "$FISCO_BIN" -c config.genesis -g config.genesis > nohup.out 2>&1 &
  echo $! > "$C2/fisco/node.pid"
  popd > /dev/null
  sleep 5
  if ! curl -s -o /dev/null -X POST http://127.0.0.1:$FISCO_WEB3 \
       -H 'Content-Type: application/json' \
       -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}'; then
    # genesis 与已存 data 不一致(op-deployer 重新部署后哈希变化)→ 清 data 重来
    log "genesis hash 与 data 不一致,清 data 重启"
    kill "$(cat "$C2/fisco/node.pid")" 2>/dev/null || true
    sleep 1
    rm -rf "$C2/fisco/data"
    ulimit -s 65520
    pushd "$C2/fisco" > /dev/null
  nohup "$FISCO_BIN" -c config.genesis -g config.genesis > nohup.out 2>&1 &
  echo $! > "$C2/fisco/node.pid"
  popd > /dev/null
    sleep 5
    curl -s -o /dev/null -X POST http://127.0.0.1:$FISCO_WEB3 \
      -H 'Content-Type: application/json' \
      -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}' || \
      die "FISCO 启动失败(见 nohup.out)"
  fi
  log "FISCO 运行中(PID $(cat "$C2/fisco/node.pid"),web3 $FISCO_WEB3 / engine $FISCO_ENGINE)"
fi

# ---------- 5. rollup.json 对齐 + L1 chain config ----------
if step_run 5; then
  step 5 "rollup.json 对齐(实际创世哈希) + L1 chain config"
  # L1 genesis = anvil block 0 哈希(不是部署块)
  L1_HASH=$(python3 -c "
import json, urllib.request
req = urllib.request.Request('http://127.0.0.1:$ANVIL_PORT',
    data=b'{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"0x0\",false],\"id\":1}',
    headers={'Content-Type':'application/json'})
print(json.loads(urllib.request.urlopen(req, timeout=10).read())['result']['hash'])
")
  # L2 genesis = FISCO 实际创世哈希
  L2_HASH=$(python3 -c "
import json, urllib.request
req = urllib.request.Request('http://127.0.0.1:$FISCO_WEB3',
    data=b'{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBlockByNumber\",\"params\":[\"0x0\",false],\"id\":1}',
    headers={'Content-Type':'application/json'})
print(json.loads(urllib.request.urlopen(req, timeout=10).read())['result']['hash'])
")
  python3 -c "
import json
r = json.load(open('$C2/rollup.json'))
r['genesis']['l1']['hash'] = '$L1_HASH'
r['genesis']['l1']['number'] = 0
r['genesis']['l2']['hash'] = '$L2_HASH'
json.dump(r, open('$C2/rollup.json', 'w'), indent=2)
print('rollup.json 对齐:L1=' + '$L1_HASH'[:16] + '... L2=' + '$L2_HASH'[:16] + '...')
"

  # L1 chain config(anvil:cancunTime=0 + blobSchedule)
  python3 -c "
import json
cfg = {'config': {
    'chainId': $ANVIL_CHAIN,
    'homesteadBlock': 0, 'eip150Block': 0, 'eip155Block': 0, 'eip158Block': 0,
    'byzantiumBlock': 0, 'constantinopleBlock': 0, 'petersburgBlock': 0,
    'istanbulBlock': 0, 'muirGlacierBlock': 0, 'berlinBlock': 0, 'londonBlock': 0,
    'arrowGlacierBlock': 0, 'grayGlacierBlock': 0, 'cancunTime': 0,
    'clique': {'period': 0, 'epoch': 30000},
    'blobSchedule': {'cancun': {'target': 3, 'max': 6, 'baseFeeUpdateFraction': 332827}},
}}
json.dump(cfg, open('$C2/l1_chain_config.json', 'w'), indent=2)
"
  echo "0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80" > "$C2/sequencer.key"
  log "rollup.json + l1_chain_config.json + sequencer.key 就绪"
fi

# ---------- 6. op-node 启动 ----------
if step_run 6; then
  step 6 "op-node sequencer 启动"
  [ -x "$C2/op-node" ] || die "op-node 不存在: $C2/op-node(需 monorepo 构建)"
  pgrep -f "op-node.*rollup.config $C2/rollup.json" | xargs kill 2>/dev/null || true
  sleep 1
  nohup "$C2/op-node" \
    --rollup.config "$C2/rollup.json" \
    --rollup.l1-chain-config "$C2/l1_chain_config.json" \
    --l1 http://127.0.0.1:$ANVIL_PORT \
    --l2 http://127.0.0.1:$FISCO_ENGINE \
    --l2.jwt-secret "$C2/fisco/jwt.hex" \
    --l2.enginekind geth \
    --l1.beacon.ignore \
    --l1.epoch-poll-interval "$OP_NODE_EPOCH_POLL" \
    --sequencer.enabled \
    --sequencer.l1-confs 1 \
    --p2p.sequencer.key "$(cat "$C2/sequencer.key")" \
    --rpc.port "$OP_NODE_PORT" \
    $OP_NODE_EXTRA_FLAGS \
    --log.level info \
    --log.format json \
    > "$C2/op-node.log" 2>&1 &
  echo $! > "$C2/op-node.pid"
  disown
  sleep 10
  BN=$(python3 -c "
import json, urllib.request
req = urllib.request.Request('http://127.0.0.1:$FISCO_WEB3',
    data=b'{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}',
    headers={'Content-Type':'application/json'})
print(json.loads(urllib.request.urlopen(req, timeout=10).read())['result'])
")
  log "op-node 启动(PID $(cat "$C2/op-node.pid"),L2 block=$BN)"
  [ "$BN" != "0x0" ] && log "✅ C2 出块中!deposit/withdraw 闭环可用"
fi

# ---------- 7. op-batcher ----------
# Post L2 unsafe blocks as batches to L1 so derivation advances cross-safe/finalized.
# The batcher key must match SystemConfig.batcherAddr (op-deployer default = anvil #2).
if step_run 7; then
  step 7 "op-batcher start"
  OP_MONOREPO="${OP_MONOREPO:-/Users/octopus/octo/code/blockchain-impl/optimism}"
  if [ ! -x "$C2/op-batcher" ]; then
    (cd "$OP_MONOREPO" && go build -o "$C2/op-batcher" ./op-batcher/cmd) \
      || die "op-batcher build failed (needs monorepo + go)"
  fi
  BATCHER_KEY=$(cast wallet private-key --mnemonic "test test test test test test test test test test test junk" --mnemonic-index 2)
  pgrep -f "$C2/op-batcher" | xargs kill 2>/dev/null || true
  sleep 1
  nohup "$C2/op-batcher" \
    --l1-eth-rpc http://127.0.0.1:$ANVIL_PORT \
    --l2-eth-rpc http://127.0.0.1:$FISCO_WEB3 \
    --rollup-rpc http://127.0.0.1:$OP_NODE_PORT \
    --private-key "$BATCHER_KEY" \
    --max-channel-duration "$BATCHER_MAX_CHANNEL" \
    --rpc.port "$OP_BATCHER_PORT" \
    --log.level debug \
    > "$C2/op-batcher.log" 2>&1 &
  echo $! > "$C2/op-batcher.pid"
  disown
  sleep 10
  if grep -q "Batch Submitter started" "$C2/op-batcher.log" 2>/dev/null; then
    log "✅ op-batcher started (PID $(cat "$C2/op-batcher.pid")), safe/finalized will advance shortly"
  else
    log "!! op-batcher may have failed to start, check $C2/op-batcher.log"
  fi
fi

# ---------- version manifest check (pinned baseline: tools/op-e2e/versions.json) ----------
# Collects OBSERVED versions into $C2/versions.json and diffs against the manifest:
#   enforced (aborts on mismatch): monorepo commit, on-chain contract versions
#   warned:  anvil / go toolchain version
#   recorded only: FISCO-BCOS EL commit (moves with development)
# Escape hatch for deliberate off-baseline experiments: SKIP_VERSION_CHECK=1
if [ -z "${SKIP_VERSION_CHECK:-}" ]; then
  # set -e: a failing command substitution inside an assignment would kill the script
  # silently, so resolve the repo root defensively (the script has cd'd to $C2 by now,
  # a relative $0 no longer resolves).
  FISCO_REPO="${FISCO_REPO:-$( (cd "$(dirname "$0")/../.." 2>/dev/null && pwd) || true )}"
  FISCO_REPO="${FISCO_REPO:-/Users/octopus/octo/code/FISCO-BCOS}"
  VERSIONS_MANIFEST="$FISCO_REPO/tools/op-e2e/versions.json"
  if [ -f "$VERSIONS_MANIFEST" ]; then
    echo
    echo "==== version manifest check ===="
    python3 - "$VERSIONS_MANIFEST" "$C2" "$MONOREPO" "$FISCO_REPO" "$ANVIL_PORT" <<'PYEOF' || die "version manifest mismatch - e2e baseline compromised; fix the environment or bump the manifest deliberately"
import json, re, subprocess, sys

manifest_path, c2, monorepo, fisco_repo, anvil_port = sys.argv[1:6]
manifest = json.load(open(manifest_path))

def sh(*cmd, cwd=None):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=30, cwd=cwd).stdout.strip()
    except Exception:
        return ""

observed = {"manifest": manifest_path}
mono_commit = sh("git", "-C", monorepo, "rev-parse", "HEAD")
# go must be probed INSIDE the monorepo: go.mod's toolchain directive selects the real
# build toolchain there, while a bare `go version` elsewhere reports the system bootstrap go.
go_raw = sh("go", "version", cwd=monorepo)
observed["op_monorepo"] = {
    "commit": mono_commit,
    "describe": sh("git", "-C", monorepo, "describe", "--tags"),
    "go": go_raw.split()[2] if len(go_raw.split()) > 2 else "unknown",
}
anvil_raw = sh("anvil", "--version")
anvil_m = re.search(r"([0-9]+\.[0-9]+\.[0-9]+)", anvil_raw)
observed["l1"] = {
    "anvil": anvil_m.group(1) if anvil_m else "unknown",
    "chain_id": manifest["l1"]["chain_id"],
}
observed["el"] = {
    "commit": sh("git", "-C", fisco_repo, "rev-parse", "HEAD"),
    "dirty_files": len(sh("git", "-C", fisco_repo, "status", "--porcelain").splitlines()),
}
try:
    gen = json.load(open(f"{c2}/l2genesis.json")).get("config", {})
    observed["fork_schedule"] = {k: v for k, v in gen.items() if k.endswith("Time")}
except Exception:
    observed["fork_schedule"] = "l2genesis.json not found"

# On-chain contract versions (needs L1 + state.json; soft-skip when unreachable).
contract_observed, contract_enforced = {}, True
try:
    deployed = json.load(open(f"{c2}/state.json"))["opChainDeployments"][0]
    for name in manifest["l1_contracts"]["expected_versions"]:
        out = sh("cast", "call", deployed[name], "version()(string)",
                 "--rpc-url", f"http://127.0.0.1:{anvil_port}")
        m = re.search(r"[0-9]+\.[0-9]+\.[0-9]+", out)
        contract_observed[name] = m.group(0) if m else "no version()"
except Exception as e:
    contract_observed["error"] = str(e)[:120]
    contract_enforced = False
observed["l1_contracts"] = contract_observed

def row(label, expected, actual, status):
    print(f"  {status:<4} {label:<36} expected={expected:<24} observed={actual}")

failures = []
print("  -- enforced --")
exp_commit = manifest["op_monorepo"]["commit"]
mono_ok = mono_commit == exp_commit
row("op_monorepo.commit", exp_commit[:16], mono_commit[:16] or "unknown", "ok" if mono_ok else "FAIL")
if not mono_ok:
    failures.append("op_monorepo.commit")
if contract_enforced:
    for name, exp in manifest["l1_contracts"]["expected_versions"].items():
        got = contract_observed.get(name, "?")
        ok = got == exp
        row(f"contract.{name}", exp, got, "ok" if ok else "FAIL")
        if not ok:
            failures.append(f"contract.{name}")
else:
    print("  WARN contracts unreachable - on-chain versions not verified this run")
print("  -- warned --")
exp_anvil = manifest["l1"]["anvil"]
row("anvil", exp_anvil, observed["l1"]["anvil"],
    "ok" if observed["l1"]["anvil"] == exp_anvil else "WARN")
exp_go = manifest["op_monorepo"]["go"]
row("go", exp_go, observed["op_monorepo"]["go"],
    "ok" if observed["op_monorepo"]["go"] == exp_go else "WARN")
print("  -- recorded (never enforced) --")
dirty = f" (dirty:{observed['el']['dirty_files']})" if observed["el"]["dirty_files"] else ""
row("el.commit (FISCO-BCOS)", ">= 5178d86db", observed["el"]["commit"][:16] + dirty, "")

observed["check"] = {"failures": failures, "enforced_contracts": contract_enforced}
json.dump(observed, open(f"{c2}/versions.json", "w"), indent=1)
print(f"  observed versions written to {c2}/versions.json")
if failures:
    print(f"  !! MISMATCH: {', '.join(failures)}")
    sys.exit(1)
print("  all enforced versions match the manifest")
PYEOF
  else
    log "!! versions manifest not found at $VERSIONS_MANIFEST - skipping check"
  fi
fi

echo
echo "=== C2 devnet 就绪 ==="
echo "  anvil L1 : http://127.0.0.1:$ANVIL_PORT (chain $ANVIL_CHAIN)"
echo "  FISCO L2 : web3 $FISCO_WEB3 / engine $FISCO_ENGINE (chain $L2_CHAIN)"
echo "  Quick reference:"
echo "    deposit : cast send <deposit_contract> \"depositTransaction(address,uint256,uint64,bool,bytes)\" <to> <amt> 100000 false 0x --value <amt> --rpc-url http://127.0.0.1:$ANVIL_PORT"
echo "    withdraw: cast send 0x4200000000000000000000000000000000000016 \"initiateWithdrawal(address,uint256,bytes)\" <l1_recipient> 100000 0x --value 1ether --rpc-url http://127.0.0.1:$FISCO_WEB3 --chain-id $L2_CHAIN"
echo "             (MessagePasser lives at 0x4200..0016 with entry point initiateWithdrawal — there is no withdraw();"
echo "              0x4200..0010 is the L2StandardBridge. Default-fee flows work: eth_feeHistory and eth_gasPrice are implemented.)"
