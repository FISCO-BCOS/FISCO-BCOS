# 2026-08-18 — eth_genesis_header state_root = allocs MPT root

让 L2 genesis 装配能力成立:**`[eth_genesis_header].state_root` 现在由纯 Python 从
genesis allocs 计算,逐字节等于 C++ `Ledger::computeGenesisStateTrie` 的 derived root**。
此前 setup 生成的 header 用空 trie root(`56e81f17…`),节点启动即报
`state_root does not match the state root derived from the genesis allocs: artifact=56e81f17…
derived=0f4dbf6c…`。

## MPT 算法理解(与 C++ 逐字节对齐)

C++ 端(`bcos-ledger/bcos-ledger/GenesisStateRoot.cpp`)构造 op-geth 兼容的 secure
Ethereum state trie:

- **State trie**(外层,secure):每个 alloc 的 leaf key = `keccak256(address20)`。
- **Leaf value** = `RLP([nonce, balance, storageRoot, codeHash])`(Ethereum StateAccount):
  - nonce/balance 为 minimal big-endian RLP 整数(0 → `0x80`);
  - storageRoot/codeHash 为 32 字节 RLP 字符串(`0xa0 ‖ 32B`)。
- **storageRoot**(每账户,secure):`keccak256(slot32) → RLP(去前导零后的 value)`。
  空 storage → `emptyRootHash()`(`56e81f17…`);零值 slot 被跳过(不入 trie)。
- **codeHash** = `keccak256(code)`;空 code → `emptyCodeHash()`(`c5d24601…`)。
- 空 alloc 集合 → `emptyRootHash()`。

**MPT 节点编码**(`bcos-ledger/bcos-ledger/mpt`):
- Leaf      = `RLP list [HP(suffix, leaf=true),  value]`
- Extension = `RLP list [HP(shared, leaf=false), child_ref_raw]`
- Branch    = `RLP list [child0..child15, value]`
- child ref:absent → `0x80`;inline(RLP < 32B)→ 原始字节;hash(RLP ≥ 32B)→ `0xa0 ‖ keccak256(raw)`。
- Root **总是** 32 字节 hash(即使顶层节点编码 < 32B 也再 keccak 一次)。

**HP(hex-prefix,compact)编码**:`0x20|leaf / 0x10|odd`,奇数长度时首字节低 4 位 = 首 nibble。

两个关键坑(初版 prototype 踩到):
1. **Leaf/Branch 的 value 也要 RLP 编码成字节串**,不能裸拼。`RLP([HP, value])` 里
   value 走 `encode(bytes)` → 有 string header;Branch 第 17 项空值 = `0x80`(不是空 payload)。
2. 排序:`std::map<h256>` = 32 字节字典序 = 64-nibble 路径序;Python 端按 keyHash bytes
   排序即可。

## 实现

- **`tools/opstack-genesis/mpt_state_root.py`**(新):纯 Python MPT(keccak 复用
  build-allocs.py)。`parse_allocs_ini()` 解析 `[alloc.N]`(含 `[alloc.N.storage]`);
  `compute_state_root(allocs)` 返回 root bytes32。`__main__` 打印 root。
- **`tools/opstack-genesis/gen_eth_header_fixture.py`**:新增 `--allocs <INI>`;给定则
  用 `mpt_state_root` 计算 state_root 填入 header,其余 21 字段与 hash 计算逻辑不变。
  默认(无 `--allocs`)输出与改动前逐字节一致(空 trie root,hash `b153f41d…`)。
- **`tools/op-e2e/setup_op_node.sh`** step 5.0:把 `$WORK/allocs.ini` + 下方合并进
  config.genesis 的 SENDER alloc(`$SENDER`/`$SENDER_BAL`)拼成 `$WORK/header_allocs.ini`,
  传给 `gen_eth_header_fixture.py --toml --allocs`。这样 header 的 state_root 覆盖
  完整 alloc 集合(13 predeploy + SENDER = 14),与节点 `computeGenesisStateTrie`
  的输入一致。

## 验证结果(state root 匹配证据)

| 输入 | 我的 Python root | 期望 | 结果 |
|---|---|---|---|
| `/tmp/op-spike/b3/config.genesis` 的 14 allocs(手工配好的已知成功配置) | `0x409e6736ad7d48c00cd82b66ccbc982f4d09cd40f008a0e2ffbc41b5d6fd36b9` | 同左(该配置已知可启动) | ✅ 逐字节一致 |
| setup 生成 allocs(`/tmp/op-e2e-ci-sim3/allocs.ini`)+ SENDER(14 allocs) | `0x0f4dbf6c371893bb3c780b4ec868dc23df861368504fcf3684663369dddfccc9` | C++ derived root(启动报错里的 `derived=`) | ✅ 逐字节一致 |

**实机端到端**(用修改后的 setup 以全新 WORK=`/tmp/op-mpt-verify` 跑 step 1-9 装配,
用现有 build 二进制启动):

- `eth_genesis_header.ini` 与合并后的 `config.genesis`:
  `state_root=0x0f4dbf6c…`,`hash=0x9562cc08…`。
- B3 与 B3a 节点**均正常启动**,无 `state_root does not match` 报错。
- C++ 日志:
  ```
  [LEDGER][#buildGenesisBlock] state_root:0x0f4dbf6c371893bb3c780b4ec868dc23df861368504fcf3684663369dddfccc9
  [LEDGER]buildGenesisBlock: eth genesis header,hash=9562cc08a4947a9e3beccede1b2f8a7d2678c9e04ad8da174fb3b8856c9c4a81
  ```
- RPC `eth_getBlockByNumber(0)` 返回 `stateRoot=0x0f4dbf6c…`、`hash=0x9562cc08…` —
  与工具输出完全一致。`eth_getCode` 可读回 predeploy bytecode(MPT state 读正常)。

即:Python 算的 root == C++ derived root == 节点实际持久化并对外服务的 genesis
stateRoot == header 里写的 state_root,四者逐字节一致。

## 遇到的坑

1. **Leaf/Branch value 必须 RLP 字节串编码**(见上),初版漏了导致 root 对不上;
   修正后两个锚点立即逐字节匹配。
2. **SENDER alloc 必须计入 root**:header 的 state_root 要匹配 config.genesis 合并后的
   *完整* alloc 集合(13 predeploy + 手动追加的 SENDER)。只算 build-allocs 的 13 个
   得到 `5c66ff60…`,≠ derived root;加上 SENDER 才得到 `0f4dbf6c…`。
3. **setup 脚本的 `-s/-e` 其实没解析**(usage 注释与实现不符),全量跑了一次 step 1-9;
   无害但要注意。
4. **run_all ALL GREEN 未达成**,原因与本改动无关(见下)。

## run_all 状态(与本改动无关的既有环境问题)

用现有二进制(d577d977,opstack-op-e2e-on-scheduler 分支)实跑 op-e2e 套件,未全绿:

- **`eth_call` 全部 `Invalid argument`**:OP 路径 `OpCallScheduler::coCallLatest` → `opValidate`
  对 eth_call 空 envelope 的校验问题(回归测试 `EmptyEnvelopeAccepted` 覆盖的正是这个
  真实节点发现);与 state_root 无关——节点 genesis 正确、`eth_getCode` 正常。
- **`a1_active` `engine_forkchoiceUpdatedV4 is not yet supported`**:V4 engine 端点是桩
  (见 memory `op-realnode-spike-v4-gate.md`),非本改动引入。
- 另有并发 build(另一 worktree)占 CPU、端口占用导致的偶发失败。

本改动的目标验证(节点能带着正确 state_root 启动并服务正确的 genesis block)已
**全部达成**;run_all 的剩余红项是 OP 执行路径既有欠账,与 genesis header 装配无关。

## 提交

`feat(genesis): compute eth-genesis-header state_root matching allocs MPT root`
(1 commit:3 个文件 —— 新增 mpt_state_root.py,改 gen_eth_header_fixture.py 与
setup_op_node.sh)。
