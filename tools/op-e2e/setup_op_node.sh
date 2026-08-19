#!/usr/bin/env bash
# setup_op_node.sh — 一键搭建 OP 模式 B3/B3a 节点 + 跑 op-e2e 套件
#
# 2026-08-17 重建验证:run_all.sh ALL OP-E2E GREEN(118 断言)。本脚本把整条链路
# (genesis 预部署 bytecode → 节点密钥/certs → 合并 config.genesis → B3a 克隆 →
# sign_secp → 启动 → 跑套件)封装为幂等脚本。每步可单独重跑(-s/-e 指定范围),
# 已完成的步骤不会重做。
#
# 用法:
#   bash setup_op_node.sh            # 全链路
#   bash setup_op_node.sh -s 5       # 从第 5 步(B3 装配)开始
#   bash setup_op_node.sh -s 1 -e 4  # 只跑 1-4 步(genesis allocs 链路)
#
# 依赖:git / python3 / forge(foundry) / openssl(系统或 brew 1.1 均可,RSA cert 不需要 1.1)/
#       clang(编 sign_secp,链接 vcpkg secp256k1)/ 网络(github.com,仅 OP-fork 步骤需要)
#
# 关键配置事实(踩坑后固化,见 memory op-e2e-node-rebuilt-config-blueprint):
#   - 单一合并 config.genesis 同时当 -c 和 -g(restart_b3.sh / start.sh 都是
#     `-c config.genesis -g config.genesis`)
#   - [executor] version=3(不是 executor_version)+ evm_revision_forks=0:prague
#   - [rpc] listen_port(不是 rpc_listen_port)
#   - [web3] chain_id 决定 eth_chainId;套件签名用 CHAIN_ID=11155111
#   - without_tars_framework=true 需要 conf/tars_proxy.ini 存在
#   - B3: enable_single_node_consensus=true + op_engine_rpc.enable=false(被动出块;
#     本分支 OP 模式由 SingleNodeConsensus 驱动块生产,两者与 engine RPC 互斥
#     "both drive the same EngineService"(NodeConfig.cpp);PBFT 不在 OP 模式初始化)
#   - B3a: enable_single_node_consensus=false + op_engine_rpc.enable=true(FCU 驱动,
#     与 a1_active 套件配合;步骤 6 clone sed 自动翻转这两个 flag)
#   - 注意:旧分支 worktree-op-alignment 上两者可共存且必须开 true——那是旧线
#     行为,在旧线的 84b3be0d 回归修复有记录;两条分支的配置契约不同
#   - [eth_genesis_header] 22 字段必须存在(feature_l2_ethereum_compat 强制,
#     state_root 必须是合并 allocs 的真实 MPT 根——--allocs 链路)
#   - B3a produce_empty_blocks=false(a1_active FCU 秒级 timestamp 竞态)
#   - SENDER genesis 余额 10^24 wei(chain_driver/b3_contracts 依赖)
set -u

# ---------- 可调参数 ----------
ROOT="${ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"       # repo 根
WORK="${WORK:-/tmp/op-spike}"                                  # 节点工作区
VENV="$WORK/venv"
BINARY="${BINARY:-$ROOT/build/fisco-bcos-air/fisco-bcos}"     # 节点二进制
L2CONTRACTS="$ROOT/bcos-l2-contracts"                          # L2 合约
OPGEN="$ROOT/tools/opstack-genesis"                            # genesis 工具
OP_PIN="$L2CONTRACTS/op-fork-pin.toml"                         # OP 版本 pin
OP_DIR="$WORK/op"                                              # OP 上游 clone 目标
SECP_PKG="$ROOT/vcpkg/packages/secp256k1_arm64-osx"           # vcpkg secp256k1
SIGN_SECP_SRC="$ROOT/tools/op-e2e/sign_secp.c"                 # sign_secp 源码(已入库)
# OP 链参数 —— 套件签名固定用 11155111,group 名决定存储路径 data/<group>
CHAIN_ID="${CHAIN_ID:-11155111}"
GROUP_ID="${GROUP_ID:-group}"
ISTHMUS_TIME="${ISTHMUS_TIME:-1000}"
JOVIAN_TIME="${JOVIAN_TIME:-2000}"
# 预资助账户(chain_driver/b3_contracts 的 SENDER)
SENDER="${SENDER:-0x6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693}"
SENDER_BAL="${SENDER_BAL:-1000000000000000000000000}"          # 10^24 wei
AUTH_ADMIN="${AUTH_ADMIN:-0x3443d6866e757893e6862f451f5d1b7976c54594}"
FEE_RECIPIENT="${FEE_RECIPIENT:-0x4200000000000000000000000000000000000011}"
# 端口(套件硬编码目标,勿改):B3 = rpc/web3/engine/p2p
B3_RPC=20211; B3_WEB3=8553; B3_ENGINE=8554; B3_P2P=31301
B3A_RPC=20212; B3A_WEB3=8563; B3A_ENGINE=8564; B3A_P2P=31302
# openssl(RSA cert 系统 3.6 即可;若要用 generate_cert.sh 的 SM2 路径才需 1.1)
OPENSSL="${OPENSSL:-$(command -v openssl)}"
# 起始/结束步骤
START="${START:-1}"; END="${END:-9}"

step() { echo; echo "==== [$1/9] $2 ===="; }
log() { echo "  >> $*"; }
die() { echo "  !! $*" >&2; exit 1; }
step_run() { [ "$1" -ge "$START" ] && [ "$1" -le "$END" ]; }

node_id_of() {  # $1=node.pem → 64 字节 secp256k1 公钥 hex(与 genesis node.0 一致)
    "$OPENSSL" ec -in "$1" -pubout -conv_form uncompressed 2>/dev/null \
        | grep -v "PUBLIC KEY" | tr -d '\n' | base64 -d | tail -c 64 | xxd -p -c 64 | tr -d '\n'
}

# ---------- 1. 环境准备 ----------
if step_run 1; then
  step 1 "环境:venv + pyyaml + forge"
  [ -x "$(command -v forge)" ] || die "forge 未安装 (foundry) — 请先安装"
  [ -x "$(command -v git)" ] || die "git 未安装"
  if [ ! -x "$VENV/bin/python" ]; then
    python3 -m venv "$VENV" || die "venv 创建失败"
  fi
  "$VENV/bin/python" -c "import yaml" 2>/dev/null || "$VENV/bin/pip" install -q pyyaml
  log "venv+pyyaml OK"
fi

# ---------- 2. 构建 bcos-l2-contracts(自研 SystemConfig/L2ValidatorSet) ----------
if step_run 2; then
  step 2 "forge build bcos-l2-contracts"
  cd "$L2CONTRACTS" || die "无 $L2CONTRACTS"
  [ -d lib/forge-std ] || git clone --quiet --depth 1 https://github.com/foundry-rs/forge-std.git lib/forge-std
  [ -d lib/openzeppelin-contracts ] || git clone --quiet --depth 1 --branch v4.7.3 \
      https://github.com/OpenZeppelin/openzeppelin-contracts.git lib/openzeppelin-contracts
  [ -d lib/openzeppelin-contracts-upgradeable ] || git clone --quiet --depth 1 --branch v4.7.3 \
      https://github.com/OpenZeppelin/openzeppelin-contracts-upgradeable.git lib/openzeppelin-contracts-upgradeable
  forge build || die "forge build bcos-l2-contracts 失败"
  log "out/ 已生成自研预部署 artifacts"
fi

# ---------- 3. OP-fork 预部署合约(vendored 优先,否则 clone 上游) ----------
if step_run 3; then
  step 3 "OP-fork 11 个预部署合约 artifacts"
  missing=0
  for n in L1Block L2ToL1MessagePasser L2CrossDomainMessenger L2StandardBridge GasPriceOracle \
           ProxyAdmin SequencerFeeVault BaseFeeVault L1FeeVault WETH OptimismMintableERC20Factory; do
    [ -f "$L2CONTRACTS/out/$n.sol/$n.json" ] || missing=1
  done
  if [ "$missing" -eq 0 ]; then
    log "OP-fork artifacts 已存在(vendored),跳过 clone"
  else
    log "OP-fork artifacts 缺失 → clone ethereum-optimism/optimism @ $(grep '^commit' "$OP_PIN" | awk '{print $3}')"
    commit=$(grep '^commit' "$OP_PIN" | awk '{print $3}')
    tag=$(grep '^tag' "$OP_PIN" | awk '{print $3}')
    if [ ! -d "$OP_DIR" ]; then
      git clone --quiet --depth 1 --branch "$tag" --single-branch \
        https://github.com/ethereum-optimism/optimism.git "$OP_DIR" || die "OP clone 失败(需网络)"
      cd "$OP_DIR" || die
      git checkout --quiet "$commit" 2>/dev/null || true
      for sub in openzeppelin-contracts openzeppelin-contracts-upgradeable solady solmate; do
        sha=$(grep -A6 '^\[deps\]' "$OP_PIN" | grep "$sub" | awk '{print $3}' | tr -d '"')
        [ -d "packages/contracts-bedrock/lib/$sub" ] || \
          git clone --quiet --depth 1 "https://github.com/OpenZeppelin/$sub.git" \
            "packages/contracts-bedrock/lib/$sub" 2>/dev/null || true
        [ -d "packages/contracts-bedrock/lib/$sub" ] || \
          git clone --quiet --depth 1 "https://github.com/Vectorized/$sub.git" \
            "packages/contracts-bedrock/lib/$sub" 2>/dev/null || true
        [ -d "packages/contracts-bedrock/lib/$sub" ] || log "WARN: 需手动补 lib/$sub"
      done
      (cd packages/contracts-bedrock && forge build) || die "OP contracts forge build 失败"
    fi
    for n in L1Block L2ToL1MessagePasser L2CrossDomainMessenger L2StandardBridge GasPriceOracle \
             ProxyAdmin SequencerFeeVault BaseFeeVault L1FeeVault WETH OptimismMintableERC20Factory; do
      src="$OP_DIR/packages/contracts-bedrock/out/$n.sol/$n.json"
      [ -f "$src" ] || { log "WARN: 未找到 $src"; continue; }
      mkdir -p "$L2CONTRACTS/out/$n.sol"
      cp "$src" "$L2CONTRACTS/out/$n.sol/$n.json"
    done
    log "OP-fork artifacts 已就位"
  fi
fi

# ---------- 4. 生成 allocs(genesis 预部署 bytecode 片段) ----------
# op-alignment-on-scheduler: build-allocs.py requires --base-allocs (the frozen
# op-deployer terminal alloc JSON, op-fork-pin.toml [karst_pin].base_allocs_sha256).
# If allocs.ini already exists (from a prior run or manual setup), skip regeneration.
if step_run 4; then
  if [ -s "$WORK/allocs.ini" ]; then
    step 4 "allocs.ini 已存在($(grep -c '^\[alloc' "$WORK/allocs.ini") alloc sections),跳过"
  else
    step 4 "build-allocs.py → allocs.ini"
    [ -f "$OPGEN/chain-config.yaml" ] || cp "$OPGEN/chain-config.template.yaml" "$OPGEN/chain-config.yaml"
    # --base-allocs: provide via BASE_ALLOCS env var (CI) or default committed path.
    # The frozen artifact is regenerated with zero secrets by setup_c2.sh:
    # anvil L1 → op-deployer init/bootstrap/apply → inspect genesis → l2genesis.json,
    # whose sha256 is pinned in chain-config-c2.yaml base_allocs_sha256.
    BASE_ALLOCS="${BASE_ALLOCS:-$OPGEN/op-fork-base-allocs.json}"
    [ -f "$BASE_ALLOCS" ] || die "base-allocs JSON not found: $BASE_ALLOCS
  Regenerate it via tools/op-e2e/setup_c2.sh (local anvil, no secrets) and copy
  \$C2/l2genesis.json to tools/opstack-genesis/op-fork-base-allocs.json"
    "$VENV/bin/python" "$OPGEN/build-allocs.py" \
        --config "$OPGEN/chain-config-c2.yaml" \
        --contracts "$L2CONTRACTS" \
        --base-allocs "$BASE_ALLOCS" \
        --out "$WORK/allocs.ini" || die "build-allocs 失败"
    log "$(grep -c '^\[alloc' "$WORK/allocs.ini") 个预部署 alloc"
  fi
fi

# ---------- 5. B3 装配:node.pem + certs + nodes.json + jwt + 合并 config.genesis ----------
if step_run 5; then
  step 5 "B3 节点密钥/certs/配置装配"
  NODE_BASE="$WORK/b3"
  mkdir -p "$NODE_BASE/conf"
  [ -x "$BINARY" ] || die "节点二进制不存在: $BINARY(需先构建)"
  # 5.0 eth_genesis_header: L2 模式必需(22 字段 + hash,NodeConfig fail-fast)。
  # 用 gen_eth_header_fixture.py 生成(独立 RLP+keccak,hash 与 C++ 测试一致)。
  # state_root 必须等于 Ledger::computeGenesisStateTrie 对 config.genesis 合并后
  # 完整 alloc 集合(含下方 [alloc.N] SENDER,动态索引)算出的 MPT root,否则
  # applyEthGenesisHeader 报 "state_root does not match" 拒绝启动。
  ETH_HEADER="$WORK/eth_genesis_header.ini"
  if [ -f "$WORK/allocs.ini" ]; then
    HEADER_ALLOCS="$WORK/header_allocs.ini"
    {
      cat "$WORK/allocs.ini"
      # SENDER alloc 与下方 config.genesis 合并处的 [alloc.N] 字段保持一致
      # (count only [alloc.N] sections, not [alloc.N.storage] subsections)
      printf '\n[alloc.%d]\naddress=%s\nbalance=%s\nnonce=0\ncode=\n' \
        "$(grep -cE '^\[alloc\.[0-9]+\]$' "$WORK/allocs.ini")" "$SENDER" "$SENDER_BAL"
    } > "$HEADER_ALLOCS"
    "$VENV/bin/python" "$OPGEN/gen_eth_header_fixture.py" --toml --allocs "$HEADER_ALLOCS" \
      > "$ETH_HEADER" || die "eth_genesis_header 生成失败"
  else
    # 无 allocs.ini(仅跑 -s 5 等局部场景):退回空 alloc 的空 trie root
    "$VENV/bin/python" "$OPGEN/gen_eth_header_fixture.py" --toml > "$ETH_HEADER" \
      || die "eth_genesis_header 生成失败"
  fi
  log "eth_genesis_header 就绪(hash=$(grep '^hash=' "$ETH_HEADER" | cut -d= -f2),state_root=$(grep '^state_root=' "$ETH_HEADER" | cut -d= -f2))"

  # 5.1 节点签名私钥 node.pem(secp256k1,与 genesis node.0 绑定)
  [ -f "$NODE_BASE/node.pem" ] || \
    "$OPENSSL" ecparam -genkey -name secp256k1 -noout -out "$NODE_BASE/node.pem"
  NODE_ID=$(node_id_of "$NODE_BASE/node.pem")
  [ ${#NODE_ID} -eq 128 ] || die "node.pem 公钥提取失败: $NODE_ID"

  # 5.2 RSA CA + 节点证书(RSA cert 的 pubkey 可被 GatewayFactory 解析,已验证)
  if [ ! -f "$NODE_BASE/conf/ssl.crt" ]; then
    ( cd "$NODE_BASE/conf" && \
      "$OPENSSL" genrsa -out ca.key 2048 2>/dev/null && \
      "$OPENSSL" req -new -x509 -days 3650 -subj "/CN=op-e2e-ca/O=fisco-bcos/OU=chain" \
        -key ca.key -out ca.crt 2>/dev/null && \
      "$OPENSSL" genrsa -out ssl.key 2048 2>/dev/null && \
      "$OPENSSL" req -new -sha256 -subj "/CN=op-e2e-node/O=fisco-bcos/OU=agency" \
        -key ssl.key -out node.csr 2>/dev/null && \
      "$OPENSSL" x509 -req -days 3650 -in node.csr -CA ca.crt -CAkey ca.key \
        -CAcreateserial -sha256 -out ssl.crt 2>/dev/null ) || die "证书生成失败"
  fi
  # without_tars_framework=true 需要 tars_proxy.ini 存在
  [ -f "$NODE_BASE/conf/tars_proxy.ini" ] || \
    echo "# in-process services: no tars endpoints" > "$NODE_BASE/conf/tars_proxy.ini"

  # 5.3 nodes.json(p2p peers,单节点 = 自己)+ jwt.hex(engine RPC 认证)
  printf '{"nodes":["127.0.0.1:%d"]}' "$B3_P2P" > "$NODE_BASE/nodes.json"
  [ -f "$NODE_BASE/jwt.hex" ] || "$OPENSSL" rand -hex 32 > "$NODE_BASE/jwt.hex"

  # 5.4 合并 config.genesis —— 同时当 -c 和 -g
  cat > "$NODE_BASE/config.genesis" <<EOF
[chain]
    sm_crypto=false
    chain_id=$CHAIN_ID
    group_id=$GROUP_ID
    isthmus_time=$ISTHMUS_TIME
    jovian_time=$JOVIAN_TIME
[consensus]
    consensus_type=pbft
    block_tx_count_limit=1000
    leader_period=1
    node.0=$NODE_ID
    enable_single_node_consensus=true
    block_interval=1000
    produce_empty_blocks=true
    fee_recipient=$FEE_RECIPIENT
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
$(cat "$ETH_HEADER")
[web3]
    chain_id=$CHAIN_ID
[service]
    without_tars_framework=true
[rpc]
    listen_ip=127.0.0.1
    listen_port=$B3_RPC
[web3_rpc]
    enable=true
    listen_ip=127.0.0.1
    listen_port=$B3_WEB3
[op_engine_rpc]
    enable=false
    listen_ip=127.0.0.1
    listen_port=$B3_ENGINE
    jwt_secret_file=jwt.hex
[storage]
    enable_cache=true
[p2p]
    listen_ip=0.0.0.0
    listen_port=$B3_P2P
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
$(cat "$WORK/allocs.ini")
[alloc.$(grep -cE '^\[alloc\.[0-9]+\]$' "$WORK/allocs.ini")]
address=$SENDER
balance=$SENDER_BAL
nonce=0
code=
EOF
  log "B3 config.genesis 就绪(node.0=$NODE_ID)"
fi

# ---------- 6. B3a 克隆:端口替换 + produce_empty_blocks=false + start.sh ----------
if step_run 6; then
  step 6 "B3a 克隆(空链,端口 8563/8564)"
  NODE_BASE="$WORK/b3"; B3A="$WORK/b3a"
  [ -f "$NODE_BASE/config.genesis" ] || die "先跑第 5 步生成 B3 config"
  mkdir -p "$B3A"
  cp -r "$NODE_BASE/conf" "$B3A/conf"
  cp "$NODE_BASE/node.pem" "$B3A/"
  cp "$NODE_BASE/config.genesis" "$B3A/"
  sed -i.bak \
      -e "s/listen_port=$B3_RPC/listen_port=$B3A_RPC/" \
      -e "s/listen_port=$B3_WEB3/listen_port=$B3A_WEB3/" \
      -e "s/listen_port=$B3_ENGINE/listen_port=$B3A_ENGINE/" \
      -e "s/listen_port=$B3_P2P/listen_port=$B3A_P2P/" \
      -e "s/enable_single_node_consensus=true/enable_single_node_consensus=false/" \
      -e "s/enable=false/enable=true/" \
      -e "s/produce_empty_blocks=true/produce_empty_blocks=false/" \
      "$B3A/config.genesis"
  rm -f "$B3A/config.genesis.bak"
  printf '{"nodes":["127.0.0.1:%d"]}' "$B3A_P2P" > "$B3A/nodes.json"
  cp "$NODE_BASE/jwt.hex" "$B3A/jwt.hex"
  cat > "$B3A/start.sh" <<'EOF'
#!/usr/bin/env bash
# Start B3a (active clone). Storage preserved across restarts.
set -u
cd "$(dirname "$0")"
BINARY=${BINARY:-@BINARY@}
[ -f node.pid ] && kill "$(cat node.pid)" 2>/dev/null
sleep 2
nohup "$BINARY" -c config.genesis -g config.genesis > nohup.out 2>&1 &
echo $! > node.pid
sleep 8
kill -0 "$(cat node.pid)" 2>/dev/null && echo "B3a RUNNING" || { echo "B3a EXITED"; tail -5 nohup.out; exit 1; }
EOF
  sed -i.bak "s|@BINARY@|$BINARY|" "$B3A/start.sh" && rm -f "$B3A/start.sh.bak"
  chmod +x "$B3A/start.sh"
  log "B3a 克隆完成(RPC :$B3A_WEB3 / engine :$B3A_ENGINE)"
fi

# ---------- 7. 构建 sign_secp 助手(libsecp256k1 可恢复签名 + 低-s) ----------
if step_run 7; then
  step 7 "sign_secp 助手构建"
  [ -f "$SIGN_SECP_SRC" ] || die "sign_secp 源码缺失: $SIGN_SECP_SRC"
  [ -f "$SECP_PKG/lib/libsecp256k1.a" ] || die "vcpkg secp256k1 缺失: $SECP_PKG(先 vcpkg install secp256k1)"
  clang -O2 -I"$SECP_PKG/include" "$SIGN_SECP_SRC" -o "$WORK/sign_secp" \
      "$SECP_PKG/lib/libsecp256k1.a" "$SECP_PKG/lib/libsecp256k1_precomputed.a" \
      || die "sign_secp 编译失败"
  log "$WORK/sign_secp 就绪"
fi

# ---------- 8. 初始化 B3(全新链)+ 启动 B3a ----------
if step_run 8; then
  step 8 "初始化 B3(清旧链)+ 启动 B3 + B3a"
  # 首次重建:清掉旧链数据(restart_b3.sh 在套件内保留存储以测 b4_persist)
  rm -rf "$WORK/b3/data" "$WORK/b3a/data"
  ( cd "$ROOT/tools/op-e2e" && bash restart_b3.sh ) || die "B3 启动失败"
  bash "$WORK/b3a/start.sh" || die "B3a 启动失败"
  sleep 2
  # 冒烟:eth_chainId + blockNumber
  echo -n "B3  " ; curl -s -m 5 -X POST -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}' http://127.0.0.1:$B3_WEB3/ ; echo
  echo -n "B3a " ; curl -s -m 5 -X POST -H "Content-Type: application/json" \
    -d '{"jsonrpc":"2.0","method":"eth_chainId","params":[],"id":1}' http://127.0.0.1:$B3A_WEB3/ ; echo
fi

# ---------- 9. 跑 op-e2e 套件 ----------
if step_run 9; then
  step 9 "运行 op-e2e 回归门"
  cd "$ROOT/tools/op-e2e"
  bash run_all.sh
fi

log "完成。可复用路径: $0"
