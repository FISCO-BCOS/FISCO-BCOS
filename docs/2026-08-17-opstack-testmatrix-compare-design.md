# FISCO opstack ↔ op-geth ↔ op-reth 测试集合严格对比

> 目标上下文:**FISCO opstack 要作为 OP-node 的执行客户端(EL)替代 op-geth 被接入**。本对比以"OP-node 驱动 EL 的完整契约"为基准,逐项落到测试套件/文件/用例粒度,回答:FISCO 已有哪些测试、相比 op-geth 与 op-reth 缺了哪些、已对齐哪些。
>
> 锚定版本:FISCO `worktree-op-alignment` HEAD;op-geth `v1.101702.2`(`e8800cffe`);op-reth `da197e45`(git tag `v1.19.2`)。生成日期 2026-08-17。

---

## 0. 对比框架

以"作为 OP-node EL 必须满足的契约"为 6+1 维度:

1. **EVM 核心语义** — 操作码/gas/EIP/预编译(EL 共识安全底座)
2. **OP fork 语义** — Regolith→Jovian 的 deposit、L1-info、DA/operator fee、SDM
3. **区块构造 & 引擎 API** — FCU/getPayload/newPayload、payload attributes 校验、无效 deposit 路径
4. **RPC 层** — eth_* 全端点、回执/块/交易序列化、OP 扩展字段、op-node 及下游依赖端点
5. **状态/存储/MPT/reorg** — statedb、归档(历史 tag)、reorg、proof
6. **预部署/系统合约** — L1Block/MessagePasser/Messenger/Bridge/SystemConfig
7. **横切:op-node 集成 / fuzz / 新特性(SDM、flashblocks、interop)**

每维度给三端清单 + FISCO 缺口评级:✅ 已对齐 / 🟡 部分对齐 / ❌ 缺失。

---

## 1. 三端测试集合总量

| 端 | 测试组织形式 | 总量 |
|---|---|---|
| **FISCO opstack** | ctest(googletest/Boost)+ t8n 差分向量 + op-e2e 真实节点脚本 | ctest **282 用例** + t8n **83 向量**(锚定 op-geth)+ op-e2e **149 断言**(7 脚本)+ DA 矩阵 **16×3** |
| **op-geth** | Go `*_test.go` 全包 + `tests/` EF 语料 runner + 外部 `op-e2e`(optimism monorepo) | **2133 个 `func Test`**;OP 专属约几十个(见 §2.2) |
| **op-reth** | Rust `#[test]`/`#[tokio::test]` + `op-test-vectors/` + Go e2e(proofs) | crates/**696 个 test**(其中 trie crate 333 为上游、OP 专属 ≈360);op-test-vectors **9 个** |

---

## 2. op-geth 测试集合清单(v1.101702.2)

### 2.1 总量与结构
- 全仓 `func Test` 共 **2133**。
- `tests/` = EF 官方语料 runner(`state_test.go`/`block_test.go`/`transaction_test.go` 的 `TestState`/`TestBlockchain`/`TestTransaction`),读 ethereum/tests 子模块 —— **本机未检出**(`git submodule status` 显示 `-d`,`tests/testdata/` 为空),需 `submodule update --init` 才能跑。

### 2.2 OP 专属测试文件(直接映射 FISCO opstack)

| 文件 | 测试函数 | 覆盖 |
|---|---|---|
| `core/types/rollup_cost_test.go` | `TestBedrockL1CostFunc`/`TestEcotoneL1CostFunc`/`TestFjordL1CostFuncMinimumBounds`/`TestFjordL1CostSolidityParity`/`TestExtract{`Bedrock,Ecotone,Fjord,Isthmus`}GasParams`/`TestFirstBlockEcotoneGasParams`/`TestNewL1CostFunc`/`TestNewOperatorCostFunc`/`TestFlzCompressLen`/`TestTotalRollupCostFunc` | **L1 数据费全 fork + operator fee + fastlz + Solidity 对拍** —— 正是 FISCO DA 矩阵对应的那套 |
| `core/types/receipt_opstack_test.go` | `TestDeriveOptimism{`Bedrock,Ecotone,Isthmus,Jovian`}TxReceipts`/`...NoOperatorFee`/`TestBedrockDepositReceiptUnchanged`/`TestReceiptEncodeIndexBugIsEnshrined`/`TestRoundTripReceipt`/`TestRoundTripReceiptForStorage`/`TestParseLegacyReceiptRLP*` | **OP 回执推导全 fork + deposit 回执 + encode-index bug 固化 + 回执 round-trip** |
| `core/types/deposit_test.go` | `TestDepositTxFrom` | deposit tx 编码 |
| `core/txpool/legacypool/legacypool_opstack_test.go` | — | OP txpool(deposit 处理) |
| `eth/catalyst/api_optimism_test.go` | `TestCheckOptimismPayload`/`TestCheckOptimismPayloadAttributes`/`TestForkChoiceUpdatedNilPayloadAttributes` | **引擎 API:payload/attributes 校验、FCU 空 attributes** |
| `eth/catalyst/superchain_test.go` | `TestSignalSuperchainV1`/`TestSignalSuperchainV1Halt` | superchain 信号(新) |
| `core/superchain_test.go` | `TestOPStackGenesis`/`TestRegistryChainConfigOverride`/`TestOPMainnetGenesisDB` | OP genesis/registry |
| `core/state_processor_test.go` | `TestStateProcessorErrors` | 状态处理错误路径 |
| `eth/gasprice/gasprice_test.go` / `feehistory_test.go` | — | **eth_gasPrice / feeHistory**(FISCO 未实现真实 gasPrice) |
| `consensus/misc/eip1559/eip1559_test.go` | — | eip1559 基础费 |

### 2.3 非 OP 专属但 EL 必备
- `core/`:blockchain(含 reorg/repair/sethead/snapshot)、block_validator、genesis、headerchain、chain_makers。
- `core/state/`:statedb、state_object、trie_prefetcher、access_events、statedb_fuzz、sync。
- `tests/fuzzers/`:bls/precompile/txpool/rlp/trie fuzz。

### 2.4 op-node 集成层(optimism monorepo `op-e2e`,非 op-geth 内)
- `op-e2e/opgeth/op_geth_test.go`:`TestPreregolith`/`TestRegolith`/`TestPreCanyon`/`TestCanyon`/`TestPreEcotone`/`TestEcotone`/`TestPreFjord`/`TestFjord`/`TestIsthmus` —— **各 fork 的 deposit nonce/gas/L1-info 语义**;`TestMissingGasLimit`/`TestTxGasSameAsBlockGasLimit`/`TestInvalidDepositInFCU`/`TestGethOnlyPendingBlockIsLatest`。
- `op-e2e/opgeth/conditional_tx_test.go`:`TestSendRawTransactionConditional{Disabled,Enabled,Forwarding}`。
- `op-e2e/opgeth/fastlz_test.go`:fastlz 压缩。
- 更大范围:`op-e2e/actions`(确定性动作测试)、`faultproofs/`、`interop/`。

---

## 3. op-reth 测试集合清单(da197e45)

### 3.1 总量
crates/ 下 `#[test]`+`#[tokio::test]` 共 **696**;其中 trie crate 333(纯上游 reth trie 套件),OP 专属 ≈360。`op-test-vectors/evm/` 9 个 JSON 向量。

### 3.2 OP 专属逐项

| 位置 | 数量 | 覆盖 |
|---|---|---|
| `op-test-vectors/evm/`(default/regolith/canyon/ecotone/isthmus/jovian) | 9 | EVM cfg/env 规格、**deposit 回执 pre/post-Canyon**(nonce 有无 version)、`l1_info_isthmus`(operator_fee_scalar/constant)、SDM(Jovian) |
| `crates/consensus/src/` | 23 | **Isthmus/Jovian `blob_gas_used` 校验**、Jovian `min_base_fee`、DA footprint 头校验、Holocene extraData eip1559 解析、**withdrawalsRoot = L2ToL1MessagePasser 存储根**(`l2tol1_message_passer_no_withdrawals`) |
| `crates/payload/` | 23 | SDM/post-exec `0x7d` builder、`PayloadId` 与 op-geth 逐字节一致、Holocene/Jovian extraData 编码、DA 大小上限 |
| `crates/evm/` | 30 | deposit 回执字段 pre/post-Canyon、**L1-info tx 真实主网块解析**(Ecotone/Fjord/Isthmus/Jovian)、OP_DEV 单块执行、SDM 门控 |
| `crates/rpc/` | 20 | **OP 回执扩展字段**(真实 Fjord 主网 tx 的 l1 fee/scalars)、operator fee 参数含/不含、`da_footprint_gas_scalar`/`blob_gas_used` post-Jovian、bedrock 转发中间件、`engine_getPayloadV5` |
| `crates/node/` | 30 | **payload attributes 校验**(Holocene/Jovian eip1559+min base fee)、e2e `estimate_gas_under_karst`(EIP-7825)、custom precompiles `0x756e69` |
| `crates/chainspec/` | 32 | OP Sepolia/Mainnet genesis、superchain registry(含 L2ToL1MessagePasser alloc)、`compute_jovian_base_fee=max(gas,blob)` |
| `crates/txpool/` | 21 | interop tx 检测 |
| `crates/post-exec-replay/` | 5 | `0x7d` 块归一化 + refund 校验 |
| `crates/primitives/` | 13 | deposit 回执 Regolith/Canyon round-trip |
| `crates/flashblocks/` | 145 | OP Flashblocks 部分块 |
| `tests/`(Go e2e,op-devstack) | — | account/storage proofs、`engine_executePayload`、`debug_executionWitness`、resync、**reorg**、prune |
| 预部署合约 | 极薄 | 仅 MessagePasser withdrawalsRoot(空)+ L1Block 间接;Messenger/Bridge/SystemConfig **无** |

---

## 4. FISCO opstack 测试集合清单(worktree-op-alignment HEAD)

### 4.1 ctest(282 用例)
| ctest | 数量 | 覆盖 |
|---|---|---|
| `BcosEvmOpstackTests` | 112 | EVM opstack 层(OpTransition 等) |
| `OpstackExecutorTests` | 10 | 执行器核心:normal transfer、zero-gas、fork revision 拒、L1+operator fee、receipt meta 存活 |
| `OpstackExecutorBlockTests` | 136 | 块执行:golden 对拍、t8n 重放、回执编码、L1Block/MessagePasser 差分向量 |
| `OpstackExecutorDetailTests` | 24 | 明细路径 |
| `DaMatrixFisco{Baseline,CheckOpgeth,CheckOpRevm}` | 16×3 | **DA 矩阵三端对拍**(FISCO/op-geth/op-revm) |

### 4.2 t8n 差分向量(83 个,锚定 op-geth)
- `opstack-executor/tests/t8n/cases/`:`ecotone 4` + `fjord 8` + `granite 1` + `isthmus 44` + `jovian 26`。
- 覆盖:预编译全矩阵(BLS/bn256/p256/expmod/blake2f/point_evaluation/wrap eip7702/value/63of64)、deposit(only/mint/failed)、L1Block 槽位、MessagePasser withdraw/write、system contracts real、DA mix、7702 setcode、空账户清理、fee env observer。
- `golden/engine/` 86 个 op-geth 锚定 golden;`generator/`(Go)+ `regen.sh` **pin op-geth HEAD** 且校验 worktree 干净。

### 4.3 op-e2e 真实节点(149 断言,7 脚本)
| 脚本 | 断言 | 覆盖 |
|---|---|---|
| `rpc_matrix.py` | 45 | eth_* 全矩阵 + engine_* V4 端点 + declared-gap stub + L1Block 预部署 getCode/未知 selector revert + 历史 tag |
| `state_verify.py` | 12 | RocksDB 直读核对块头 hash/baseFee/timestamp 链一致性 |
| `chain_driver.py` | 31 | 确定性签名交易入块、回执/余额/头断言、index0 deposit |
| `b4_persist.py` | 3 | 重启不丢存储、出块恢复 |
| `b3_contracts.py` | 12 | 合约部署 getCode==runtime、contractAddress==CREATE 推导、REVERT 行为 |
| `predeploy_matrix.py` | 30 | **5 预部署合约行为矩阵**(L1Block 10 getter + reject、MessagePasser withdraw + withdrawalsRoot、Messenger nonce+事件、Bridge mint/burn(DIVERGENCE)、SystemConfig get/set/owner) |
| `a1_active.py` | 16 | 引擎 API V4 全流程:FCU→getPayload→newPayload、V1-V3 版本门(-38005)、篡改 stateRoot/gasUsed/receiptsRoot 拒绝、未知 head SYNCING |

### 4.4 关键事实
- op-e2e **不接 op-node**:用 FISCO 自有 PBFT 单节点共识当出块器(deposit 由节点每块注入),无 L1 mock、无真实 L1→L2 派生。
- 回执扩展字段(opStackMeta 13 字段:l1 fee/operator fee/DA/deposit nonce/version)已对齐 op-geth `MarshalReceipt`;`OpReceiptEncodeTest` 黄金字节对拍。

---

## 5. 逐维度缺口分析(以"FISCO 作为 OP-node EL"为目标)

### 5.1 EVM 核心语义
| 端 | 覆盖 |
|---|---|
| op-geth | EF 全量语料 + core 单测 |
| op-reth | 依赖 revm(上游);无 EF runner |
| FISCO | t8n 83 精选向量 + 112 EVM 单测 |

- FISCO 预编译矩阵覆盖比 op-reth 更全;但**缺 EF 官方全量语料**。
- **评级:🟡 部分对齐**。缺口 = EF 全量语料(blockchain/state/transaction)。共识安全刚需,是 EL 替代的底线项。

### 5.2 OP fork 语义(deposit/L1-info/DA/fee)
| 端 | 覆盖 |
|---|---|
| op-geth | `rollup_cost_test.go` 全 fork L1 cost + `receipt_opstack_test.go` 回执全 fork + `op-e2e/opgeth` **各 fork deposit nonce/gas/L1-info**(Preregolith→Isthmus) |
| op-reth | `l1.rs` 真实主网块解析(Ecotone→Jovian)、consensus 的 blob_gas_used/min_base_fee、rpc 回执字段;**无 per-tx DA/fee 数学单测** |
| FISCO | DA 矩阵 16×3(**operator cost 四端逐位一致,抓到 solidity uint32 bug**)+ t8n deposit 向量 + OpReceiptEncode |

- **FISCO DA/fee 对拍是强项**(>= 两端)。但 **fork 时代 deposit 语义(Regolith/Canyon/Ecotone/Fjord 的 deposit nonce/gas/L1-info tx)缺专用向量**——op-geth 有 op-e2e/opgeth 整组,op-reth 有部分。
- **评级:🟡 部分对齐**。缺口 = 历史 fork 的 deposit 专用向量(易迁移:转 t8n)。

### 5.3 区块构造 & 引擎 API
| 端 | 覆盖 |
|---|---|
| op-geth | `eth/catalyst` payload/attributes 校验 + FCU 空 attributes + op-e2e 无效 deposit 走 FCU |
| op-reth | `node/engine.rs` attributes 校验(Holocene/Jovian)+ payload builder(SDM/DA cap)+ Go e2e executePayload |
| FISCO | `a1_active.py` 16:V4 全流程 + 篡改三字段拒绝 + 版本门 + 未知 head SYNCING |

- FISCO 覆盖主路径 + 篡改向量,但**缺:FCU 空 payload attributes、无效 deposit 走 FCU 全链路**这两个 op-geth 边界用例。
- **评级:🟡 部分对齐**。缺口 = 2 个引擎边界用例。

### 5.4 RPC 层
| 端 | 覆盖 |
|---|---|
| op-geth | eth_*/ethclient/gasprice/feehistory/rpc 各包 |
| op-reth | rpc 20(回执扩展字段真实主网、bedrock 转发中间件、pending block) |
| FISCO | rpc_matrix 45(全端点 + declared-gap stub)+ predeploy_matrix(eth_call/sendRawTransaction)+ 回执 OP 扩展字段 |

- **缺口(关键)**:
  - **`eth_gasPrice` 硬编码 0x0**(rpc_matrix 断言过)——op-geth/op-reth 都有真实实现。op-node 及钱包会依赖。
  - **历史 block tag 走不了**(OP 无 MPT 归档,已裁定"响亮拒绝"现状保留)——op-geth/op-reth 支持历史查询。
  - **`eth_getProof`/`debug_*`**:op-reth proofs e2e 有、op-geth 有;FISCO 未见。
- **评级:🟡 部分对齐**(当前 fork 内端点较全,但 gasPrice/历史/proof 三块缺失,直接影响 EL 替代)。

### 5.5 状态/存储/MPT/reorg
| 端 | 覆盖 |
|---|---|
| op-geth | statedb/trie_prefetcher/statedb_fuzz + blockchain reorg/repair/snapshot |
| op-reth | trie crate 333(上游)+ Go e2e reorg/prune/proof |
| FISCO | state_verify 12(RocksDB 直读)+ Storage2StateHelpers;无 MPT 归档、无 reorg 测试、无 statedb fuzz |

- **评级:❌ 缺失较多**。reorg、MPT 归档、statedb 级 fuzz 均无;这是 EL 替代的长尾风险(reorg 场景未测)。

### 5.6 预部署/系统合约
| 端 | 覆盖 |
|---|---|
| op-geth | 主要靠 op-e2e Go 流程(deposit/withdraw/cross-domain) |
| op-reth | **极薄**:仅 MessagePasser withdrawalsRoot(空)+ L1Block 间接 |
| FISCO | **predeploy_matrix 30 断言(5 合约行为矩阵)——三端中最系统** + t8n L1Block 槽位/MessagePasser withdraw |

- **评级:✅ FISCO 领先**。这是 FISCO 相对两端的强项(单位置行为矩阵最全)。缺口仅在**端到端 withdraw→L1 finalize 流程**(需 L1 mock)。

### 5.7 横切:op-node 集成 / fuzz / 新特性
| 项 | op-geth | op-reth | FISCO |
|---|---|---|---|
| **op-node 驱动 EL** | ✅ op-e2e 全栈(deposit 从 L1 派生) | ✅ tests/ Go e2e + node e2e | ❌ **无**(自有 PBFT 出块器,无 L1 mock) |
| fuzz | ✅ tests/fuzzers | 🟡 primitives proptest | ❌ 无 |
| SDM/post-exec `0x7d` | 🟡(feature 编码测试) | ✅ 强(payload/evm/post-exec-replay) | ❌ 未实现/未测 |
| flashblocks | — | ✅ 145 | ❌ |
| interop | ✅ op-e2e/interop | ✅ txpool interop | ❌ |
| superchain 信号 | ✅ catalyst | 🟡 registry | ❌ |

- **评级:❌ op-node 集成为最大缺口**。目标"FISCO 作为 OP-node EL"的验证恰恰需要这个:真实 op-node 发 FCU/attrs → FISCO 出块 → op-node 派生,以及 L1 deposit 流程。

---

## 6. 结论:FISCO 已有哪些 / 缺哪些

### ✅ FISCO 已具备且相对两端不弱(甚至领先)
1. **DA/operator fee 共识对拍**:DA 矩阵 16×3,四端逐位一致,抓到过 solidity uint32 bug —— 与 op-geth `rollup_cost_test.go` 同源同强度。
2. **OP 回执推导**:`OpReceiptEncodeTest` 黄金字节 + opStackMeta 13 字段对齐 op-geth `MarshalReceipt`。
3. **预部署合约行为矩阵**:predeploy_matrix 30 断言,三端中最系统(op-geth 靠 Go e2e、op-reth 极薄)。
4. **预编译覆盖**:t8n 的 isthmus/jovian 预编译矩阵比 op-reth 全。
5. **t8n 差分基建**:83 向量 + golden,reg 脚本 pin op-geth,增量向量易扩展。

### 🟡 部分对齐(需补强)
6. **历史 fork deposit 语义**(Regolith/Canyon/Ecotone/Fjord):op-geth op-e2e/opgeth 整组缺失 → 易迁移,转 t8n 向量。
7. **引擎 API 边界**:FCU 空 attributes、无效 deposit 走 FCU 两个用例缺失。
8. **RPC**:eth_gasPrice(硬编码 0x0)、历史 tag(无归档)、eth_getProof/debug_* 缺失。

### ❌ 完全缺失(EL 替代的硬缺口)
9. **op-node 集成测试**:最大缺口。无 L1 mock、无真实 L1→L2 deposit 派生、无 op-node 驱动的出块。这是"替代 op-geth 成为 OP-node EL"的验收核心。
10. **reorg / MPT 归档**:reorg 无测试;历史查询无归档。
11. **EF 全量语料**:t8n 是精选,非全量。
12. **fuzz / SDM / flashblocks / interop**:全缺(新特性,faultproof/interop 属架构差异)。

---

## 7. 建议迁移优先级(针对"OP-node EL 替代"目标)

| 优先级 | 迁移项 | 来源 | 落点 |
|---|---|---|---|
| P0 | **op-node 集成 harness**:L1 mock + op-node 发 FCU/attrs → FISCO 出块 → 派生验证 + deposit/withdraw 端到端 | op-geth `op-e2e`(Go 全栈)/ op-reth `tests/` | FISCO 新 e2e 层(L1 mock + engine 驱动) |
| P0 | **EF 官方语料**:state/blockchain/transaction 在 executor_version=3 跑通 | op-geth `tests/`(ethereum/tests) | FISCO t8n runner 接全量语料 |
| P1 | **历史 fork deposit 语义向量** | op-geth `op-e2e/opgeth/op_geth_test.go`(Preregolith→Isthmus) | 转 t8n 向量 |
| P1 | **引擎 API 边界**:FCU 空 attributes、无效 deposit 走 FCU | op-geth `eth/catalyst` + `opgeth` | `a1_active.py` 补 2 用例 |
| P2 | **RPC 补齐**:eth_gasPrice、eth_getProof、debug_*;历史 tag 决策 | op-geth `gasprice` / op-reth `rpc` | rpc_matrix 补断言 + 实现 |
| P2 | **reorg 测试** | op-reth `tests/proofs/reorg_test.go` | 需存储层支持 |
| P3 | **SDM/post-exec `0x7d`** | op-reth `post-exec-replay`/`payload` | 若目标支持该新特性 |

---

## 8. 附:三端关键文件索引(便于核对)

- FISCO:t8n `opstack-executor/tests/t8n/{cases,generator,golden}`、op-e2e `tools/op-e2e/`、回执 `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp`、meta `bcos-framework/bcos-framework/protocol/TransactionReceipt.h:37`。
- op-geth:OP 专属 `core/types/{rollup_cost,receipt_opstack,deposit}_test.go`、`eth/catalyst/api_optimism_test.go`、`core/superchain_test.go`、EF runner `tests/{state,block,transaction}_test.go`。
- op-reth:共识 `crates/consensus/src/{lib.rs,proof.rs,validation/isthmus.rs}`、L1-info `crates/evm/src/l1.rs`、回执 `crates/rpc/src/eth/receipt.rs`、向量 `op-test-vectors/evm/`、builder `crates/payload/src/builder/tests.rs`。
