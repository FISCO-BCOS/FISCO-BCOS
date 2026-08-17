# FISCO opstack ↔ op-geth ↔ op-reth 测试集合严格对比

> 目标上下文:**FISCO opstack 要作为 OP-node 的执行客户端(EL)替代 op-geth 被接入**。本对比以"OP-node 驱动 EL 的完整契约"为基准,逐项落到测试套件/文件/用例粒度,回答:FISCO 已有哪些测试、相比 op-geth 与 op-reth 缺了哪些、已对齐哪些。
>
> 锚定版本:FISCO `worktree-op-alignment` HEAD;op-geth `v1.101702.2`(`e8800cffe`);op-reth `da197e45`(git tag `v1.19.2`)。生成日期 2026-08-17,**2026-08-17 经 4 子代理并行审查后修订**(op-geth 侧 / op-reth 侧 / FISCO 侧 / 目标对齐)。

---

## 0. 对比框架

以"作为 OP-node EL 必须满足的契约"为 6+1 维度:

1. **EVM 核心语义** — 操作码/gas/EIP/预编译(EL 共识安全底座)
2. **OP fork 语义** — Regolith→Jovian 的 deposit、L1-info、DA/operator fee、SDM
3. **区块构造 & 引擎 API** — FCU/getPayload/newPayload、payload attributes 校验、无效 deposit 路径、forkchoice 状态机
4. **RPC 层** — eth_* 全端点、回执/块/交易序列化、OP 扩展字段、**op-node 下游依赖的负载路径**(deposit tx round-trip / getProof / safe-finalized)
5. **状态/存储/MPT/reorg/同步** — statedb、归档(历史 tag)、reorg、**EL 状态同步**
6. **预部署/系统合约** — L1Block/MessagePasser/Messenger/Bridge/SystemConfig
7. **横切:op-node 集成 / fuzz / 新特性(SDM、flashblocks、interop、fault proof)**

每维度给三端清单 + FISCO 缺口评级:✅ 已对齐 / 🟡 部分对齐 / ❌ 缺失。

---

## 1. 三端测试集合总量

| 端 | 测试组织形式 | 总量 |
|---|---|---|
| **FISCO opstack** | ctest(googletest/Boost)+ t8n 差分 + op-e2e 真实节点 + **RPC/MPT/EEST 单测 + CI 集成门** | ctest **282 用例** + **bcos-rpc ~140** + **MPT/trie 147** + **EEST runner**(EF 语料 12-fixture 冒烟)+ t8n **83 正向 + 46 负向向量**(锚定 op-geth)+ op-e2e **149 断言**(7 脚本)+ DA 矩阵 **16×3** + CI 集成门 **8 场景** |
| **op-geth** | Go `*_test.go` 全包 + `tests/` EF 语料 runner + 外部 `op-e2e`(optimism monorepo) | **2133 个 `func Test`**;OP 专属约 40+(含树内 miner/interop/DA/deposit-RPC) |
| **op-reth** | Rust `#[test]`/`#[tokio::test]` + `op-test-vectors/` + **op-revm 姊妹 crate fee 数学单测** + Go e2e(proofs) | crates/**696 个 test**(trie 333 上游、OP 专属 ≈360)+ op-test-vectors **9 个** + op-revm fee-math 单测一组 |

> ⚠️ 修订注(审查 C):FISCO 侧"282 用例"低估了总量——bcos-rpc 的 RPC/engine 单测(~140)、bcos-ledger 的 MPT/trie/proof 套件(147)、EEST runner 均为独立于 ctest 282 之外的覆盖,spec 已并入 §4。

---

## 2. op-geth 测试集合清单(v1.101702.2)

### 2.1 总量与结构
- 全仓 `func Test` 共 **2133**(已实测)。
- `tests/` = EF 官方语料 runner(`state_test.go`/`block_test.go`/`transaction_test.go` 的 `TestState`/`TestBlockchain`/`TestTransaction`),读 ethereum/tests 子模块 —— **本机未检出**(`git submodule status` 显示 `-d`,`tests/testdata/` 为空),需 `submodule update --init` 才能跑。

### 2.2 OP 专属测试文件(直接映射 FISCO opstack,均已逐函数核对存在)

| 文件 | 测试函数 | 覆盖 |
|---|---|---|
| `core/types/rollup_cost_test.go` | `TestBedrockL1CostFunc`/`TestEcotoneL1CostFunc`/`TestFjordL1CostFuncMinimumBounds`/`TestFjordL1CostSolidityParity`/`TestExtract{`Bedrock,Ecotone,Fjord,Isthmus`}GasParams`/`TestFirstBlockEcotoneGasParams`/`TestNewL1CostFunc`/`TestNewOperatorCostFunc`/`TestFlzCompressLen`/`TestTotalRollupCostFunc` | **L1 数据费全 fork + operator fee + fastlz + Solidity 对拍** —— FISCO DA 矩阵对应的那套 |
| `core/types/receipt_opstack_test.go` | `TestDeriveOptimism{`Bedrock,Ecotone,Isthmus,Jovian`}TxReceipts`/`TestDeriveOptimismIsthmusTxReceiptsNoOperatorFee`(**仅 Isthmus 有 NoOperatorFee 变体**)/`TestBedrockDepositReceiptUnchanged`/`TestReceiptEncodeIndexBugIsEnshrined`/`TestRoundTripReceipt*`/`TestParseLegacyReceiptRLP*` | **OP 回执推导全 fork + deposit 回执 + encode-index bug 固化 + round-trip** |
| `core/types/deposit_test.go` | `TestDepositTxFrom` | deposit tx 编码 |
| `core/txpool/legacypool/legacypool_opstack_test.go` | `TestInvalidRollupTransactions`/`TestRollupTransactionCostAccounting`/`TestRollupCostFuncChange` | OP txpool:无效 rollup tx 拒、费用核算 |
| `eth/catalyst/api_optimism_test.go` | `TestCheckOptimismPayload`/`TestCheckOptimismPayloadAttributes`/`TestForkChoiceUpdatedNilPayloadAttributes` | **引擎 API:payload/attributes 校验、FCU 空 attributes** |
| `eth/catalyst/superchain_test.go` | `TestSignalSuperchainV1`/`TestSignalSuperchainV1Halt` | superchain 信号(新) |
| `core/superchain_test.go` | `TestOPStackGenesis`/`TestRegistryChainConfigOverride`/`TestOPMainnetGenesisDB` | OP genesis/registry |
| `superchain/superchain_test.go` | `TestGetSuperchain`/`TestGetChain`/`TestGetDepset`/`TestEmbeddedRegistryCommit` | **superchain registry 整包**(嵌入 registry commit 固化) |
| `core/state_processor_test.go` | `TestStateProcessorErrors` | 状态处理错误路径 |
| `eth/gasprice/optimism-gasprice_test.go` | `TestSuggestOptimismPriorityFee` | **OP L1 费优先价建议** |
| `consensus/misc/eip1559/eip1559_optimism_test.go` | `TestCalcBaseFeeOptimism`/`TestCalcBaseFeeOptimismHolocene`/`TestCalcBaseFeeJovian`/`TestValidateHolocene1559Params`/`TestValidateHoloceneExtraData` | **OP base fee(Holocene/Jovian)+ Holocene eip1559 参数校验** |
| `consensus/misc/eip4844/eip4844_test.go` | `TestCalcBlobFeeOPStack` | OP-Stack blob fee |
| `consensus/misc/create2deployer_test.go` | `TestEnsureCreate2Deployer` | **Create2Deployer 预部署**(确定性地址) |
| `internal/ethapi/api_test.go` | `TestNewRPCTransactionDepositTx`/`TestRPCTransactionDepositTxWithVersion`/`TestUnmarshalRpcDepositTx`/`TestNewRPCTransactionOmitIsSystemTxFalse` | **deposit tx 的 RPC 转换**(eth_getTransactionByHash)、isSystemTx 省略 |
| `core/types/transaction_marshalling_test.go` | `TestTransactionUnmarshalJsonDeposit` | deposit tx JSON round-trip |
| `core/types/block_test.go` | `TestCheckTransactionConditional` | **OP 条件交易校验**(blockNumberMin/Max/timestamp) |
| `core/vm/contracts_test.go` | `TestPrecompiledP256VerifyFjord`/`TestPrecompileJovianInputSizeLimits` | **Fjord P256 + Jovian 预编译输入长度限制** |
| `eth/gasprice/gasprice_test.go` / `feehistory_test.go` | `TestSuggestTipCap`/`TestFeeHistory` | **eth_gasPrice / feeHistory**(FISCO 未实现真实 gasPrice) |

### 2.3 树内 miner/拦截器/interop(审查 A 补,spec 初版漏)

| 文件 | 测试函数 | 覆盖 |
|---|---|---|
| `miner/miner_optimism_test.go` | `TestDAFootprintMining` | **Jovian DA-footprint 块级 gas 限制**(miner)——FISCO DA 矩阵的直接对拍端 |
| `miner/op_interop_miner_test.go` | `TestInteropTxRejectedWithFailsafe`/`TestFailsafeDetection` | interop tx + supervisor failsafe |
| `miner/payload_building_test.go` | `TestBuildPayload*`/`TestDAFilters`/`TestPayloadId`/`TestDeterministicPayloadId` | **payload 构造 + DA 过滤 + 确定性 payload ID**(跨客户端对拍,op-reth 的 PayloadId parity 对应端) |
| `core/txpool/ingress_filters_test.go` | `TestInteropFilter`/`TestInteropFilterRPCFailures` | interop tx 过滤(含网络错误/超时路径) |
| `core/txpool/legacypool/legacypool_test.go` | `TestInteropTxDroppedOnReorg` | **interop tx 在 reorg 时被丢弃**(关联 §5.5 reorg) |
| `core/types/interoptypes/interop_test.go` | `TestSafetyLevel`/`TestTxToInteropAccessList` | interop 安全级排序 + 跨 L2 inbox access-list 提取 |

### 2.4 非 OP 专属但 EL 必备
- `core/`:blockchain(文件 `blockchain_test.go`/`blockchain_repair_test.go`/`blockchain_sethead_test.go`/`blockchain_snapshot_test.go` —— **非目录**)、block_validator、genesis、headerchain、chain_makers。
- `core/state/`:statedb、state_object、trie_prefetcher、access_events、statedb_fuzz、sync。
- `tests/fuzzers/`:**bls12381、bn256、difficulty、rangeproof、secp256k1、txfetcher**(审查 A 修正:无独立 txpool/rlp/trie fuzzer;rlp/trie fuzz 在各自包内)。

### 2.5 op-node 集成层(optimism monorepo `op-e2e`,非 op-geth 内)
- `op-e2e/opgeth/op_geth_test.go`:`TestPreregolith`/`TestRegolith`/`TestPreCanyon`/`TestCanyon`/`TestPreEcotone`/`TestEcotone`/`TestPreFjord`/`TestFjord`/`TestIsthmus` —— **各 fork deposit nonce/gas/L1-info 语义**;`TestMissingGasLimit`/`TestTxGasSameAsBlockGasLimit`/`TestInvalidDepositInFCU`/`TestGethOnlyPendingBlockIsLatest`。
- `op-e2e/opgeth/conditional_tx_test.go`、`fastlz_test.go`。
- `op-e2e/actions`(确定性动作)、`faultproofs/`、`interop/`。
- ⚠️ 审查 A:P0/P1 迁移来源(op-node harness、fork deposit 向量)**不在 op-geth 仓库内**,而在外部 optimism monorepo —— FISCO 需另行拉取。

---

## 3. op-reth 测试集合清单(da197e45)

### 3.1 总量
crates/ 下 `#[test]`+`#[tokio::test]` 共 **696**(实测);trie crate 333(纯上游),非-trie 363,其中 OP 专属 ≈360(审查 B:略高估,部分为通用 reth EVM/RPC 测试)。`op-test-vectors/evm/` 9 个 JSON 向量(审查 B:**未被任何 Rust 测试消费,是外部 golden fixture**,类似 FISCO t8n;生成器 `config_tv.rs` 引用 commit 在本 checkout 不存在)。

### 3.2 OP 专属逐项(审查 B 全部核对无误)

| 位置 | 数量 | 覆盖 |
|---|---|---|
| `op-test-vectors/evm/`(default/regolith/canyon/ecotone/isthmus/jovian) | 9 | EVM cfg/env 规格、deposit 回执 pre/post-Canyon、`l1_info_isthmus`(operator_fee_scalar/constant)、SDM(Jovian) |
| `crates/consensus/src/` | 23 | **Isthmus/Jovian `blob_gas_used` 校验**、Jovian `min_base_fee`、DA footprint 头校验、Holocene extraData eip1559 解析、**withdrawalsRoot = L2ToL1MessagePasser 存储根**(`l2tol1_message_passer_no_withdrawals`,空态) |
| `crates/payload/` | 23 | SDM/post-exec `0x7d` builder、`PayloadId` 与 op-geth 逐字节一致、Holocene/Jovian extraData、DA 大小上限、**interop failsafe 排除**(审查 B 补) |
| `crates/evm/` | 30 | deposit 回执字段 pre/post-Canyon、**L1-info 真实主网块解析**(Bedrock `sanity_l1_block` 2100/Ecotone 118024092/Fjord 124665056/Isthmus/Jovian)、OP_DEV 单块执行、SDM 门控 |
| `crates/rpc/` | 20 | **OP 回执扩展字段**(真实 Fjord 主网 tx:l1_gas_used=1600/l1_fee=191150293412/scalars)、operator fee 参数含/不含、`da_footprint_gas_scalar`/`blob_gas_used` post-Jovian、bedrock 转发中间件(8)、`engine_getPayloadV5` |
| `crates/node/` | 30 | **payload attributes 校验**(Holocene/Jovian eip1559+min base fee)、e2e `estimate_gas_under_karst`(EIP-7825)、custom precompiles `0x756e69` |
| `crates/chainspec/` | 32 | OP Sepolia/Mainnet genesis、superchain registry(含 L2ToL1MessagePasser alloc)、`compute_jovian_base_fee=max(gas,blob)` |
| `crates/exex/`(审查 B 补) | 11 | **OP proofs-store exex**(chain-committed/reorged/reverted/prune/resync-gap) |
| `crates/txpool/` | 21 | interop tx 检测 |
| `crates/post-exec-replay/` | 5 | `0x7d` 块归一化 + refund 校验 |
| `crates/primitives/` | 13 | deposit 回执 Regolith/Canyon round-trip |
| `crates/flashblocks/` | 145 | OP Flashblocks 部分块 |
| `tests/`(Go e2e,op-devstack) | — | account/storage proofs、`engine_executePayload`、`debug_executionWitness`、resync、**reorg**、prune |
| 预部署合约 | 极薄 | 仅 MessagePasser withdrawalsRoot(空)+ L1Block 间接;Messenger/Bridge/SystemConfig **无** |

### 3.3 审查 B 的重要公平性修正
- **"无 per-tx DA/fee 数学单测"在 op-reth 仓库内成立,但姊妹 crate op-revm 完整覆盖**:`op-revm/src/l1block.rs` 的 `calculate_tx_l1_cost_bedrock/ecotone/fjord`、`operator_fee_charge_inner/refund`、`fast_lz.rs` 的 `test_flz_compress_len`/`test_flz_native_evm_parity` —— 与 op-geth `rollup_cost_test.go` 等价。**结论:op-reth 的 L1 fee 覆盖应记作已对齐,而非低于 op-geth**。

### 3.4 审查 B 确认的负向声明(在 op-reth 内均成立)
- Canyon L1-info tx / system-tx 排序**无直接测试**(`validation/canyon.rs` 无 `#[cfg(test)]`)。
- **无 withdraw 执行态迁移测试**(仅 withdrawals-root 校验 + `validate_withdrawals_presence` 代码)。
- 预部署合约无 Messenger/Bridge/SystemConfig 专用测试。

---

## 4. FISCO opstack 测试集合清单(worktree-op-alignment HEAD)

### 4.1 ctest(282 用例,全部实测)
| ctest | 数量 | 覆盖 |
|---|---|---|
| `BcosEvmOpstackTests` | 112 | **远不止 OpTransition**:16 套件含 `RollupCostSuite`/`OpDepositSuite`/`OpFeeParamsSuite`/`OpPredeploysSuite`/`OpReceiptMetaSuite`/`OpZeroDiffSuite`/`OpFloorGasSuite`/`Op7702Suite`(审查 C 修正) |
| `OpstackExecutorTests` | 10 | 执行器核心:normal transfer、zero-gas、fork revision 拒、L1+operator fee、receipt meta 存活 |
| `OpstackExecutorBlockTests` | 136 | 块执行:golden 对拍、t8n 重放、回执编码、L1Block/MessagePasser 差分向量 |
| `OpstackExecutorDetailTests` | 24 | 明细路径 |
| `DaMatrixFisco{Baseline,CheckOpgeth,CheckOpRevm}` | 16×3 | **DA 矩阵三端对拍**(FISCO/op-geth/op-revm) |

### 4.2 t8n 差分(锚定 op-geth,审查 C 补负向向量)
- `cases/*.in.json` **83**(ecotone 4 + fjord 8 + granite 1 + isthmus 44 + jovian 26),全部 op-geth-pinned(generator_commit `e8800cffe`)。
- **`vectors/` 另有 131 个 JSON(83 正向 + 46 `invalid_*`:corrupt 12 / static 10 / invalid-tx 18 / chain-fork 6)**;`OpT8nReplayTest` 注册重放 **129** 个,含负向路径。审查 C:spec 初版只数了正向 83,漏了负向覆盖。
- `golden/engine/` 86(83 per-case + `chained/` 12 链式 golden + manifest + SHA256SUMS);`regen.sh` pin op-geth HEAD 且校验 worktree 干净。

### 4.3 op-e2e 真实节点(149 断言,7 脚本,全部实测)
| 脚本 | 断言 | 覆盖 |
|---|---|---|
| `rpc_matrix.py` | 45 | eth_* 全矩阵 + engine_* V4 端点 + declared-gap stub + L1Block 预部署 getCode/未知 selector revert + 历史 tag 路由 |
| `state_verify.py` | 12 | RocksDB 直读核对块头 hash/baseFee/timestamp 链一致性 |
| `chain_driver.py` | 31 | 确定性签名交易入块、回执/余额/头断言、index0 deposit |
| `b4_persist.py` | 3 | 重启不丢存储、出块恢复 |
| `b3_contracts.py` | 12 | 合约部署 getCode==runtime、contractAddress==CREATE 推导、REVERT 行为 |
| `predeploy_matrix.py` | 30 | **5 预部署合约行为矩阵**(L1Block 10 getter+reject、MessagePasser withdraw+withdrawalsRoot、Messenger nonce+事件、Bridge mint/burn(**DIVERGENCE**)、SystemConfig get/set/owner) |
| `a1_active.py` | 16 | 引擎 API:FCU **V4**→getPayload V4→newPayload V4、V1-V3 版本门(-38005)、篡改 stateRoot/gasUsed/receiptsRoot 拒绝、未知 head SYNCING |

> ⚠️ 审查 D 警示:`a1_active.py` 主路径用 **FCU V4**,但 op-node 实际发 **FCU V3**(见 §5.3)——这是 spec 修订要重点纠正的点。

### 4.4 补充覆盖(审查 C 补,spec 初版漏,均为真实存在)
| 位置 | 规模 | 覆盖 |
|---|---|---|
| `bcos-rpc/test/unittests/rpc/` | ~140 | **RPC/engine 单测**:`Web3ResponseTest`(10,含 OP 回执扩展字段/deposit tx 响应)、`Web3TypeTest`(14,depositRoundtrip/GoldenEncoding、EIP-7702/4844)、`EngineRpcTest`(13,FCU/newPayload/getPayload V1-V4)、`EngineHelperTest`(5)、`Web3EthCallBlockTagTest`(4,**历史 tag 路由 callAtBlock**)、`EthEndpointSystemConfigTest`(2)等 |
| `bcos-rpc` **eth_getProof** | 14(4 文件) | `EthGetProofRpcTest`(5)/`EthGetProofSlotNotInMPTTest`(4)/`EthGetProofReaderWiringTest`(3)/`EthGetProofIntegrationTest`(2);`EthGetProofRpcTest` 设 `feature_l2_ethereum_compat`(OP 模式) |
| `bcos-ledger/test/unittests/mpt/` | 147(25 文件) | **MPT/trie/proof 套件**:`EthereumTrieVectorsTest`/`ProofGenerateTest`/`ProofVerifyTest`/`ProofSlotNotInMPTTest`/`MPTBuilder*` |
| EEST runner | 2 ctest | `ethereum-executor/`(BCOS2Evmone 桥)+ `EESTRunner.cpp` + fixture 下载机制 pin **ethereum/execution-spec-tests v5.4.0 + ethereum/tests c67e485ff**;`EestRunner` 为 12-fixture/473-test 冒烟子集,基线 `baseline-fails-v540.json=[]`(100% 过) |
| `bcos-evm-eth-tests` | 5 | `EthTransitionTest.cpp`:withdrawals gwei→wei、EIP-7702 跨链 auth skip、blob 拒绝 |
| `bcos-l2-contracts` forge | — | `SystemConfig.t.sol`/`L2ValidatorSet.t.sol`(自研预部署合约 Solidity 测试) |
| `tools/opstack-genesis/test_build_allocs.py` | 5 | genesis alloc 构建 pytest |
| CI 集成门 `tools/.ci/l2-integration/` | 8 场景 | `genesis-bootstrap.sh`/`system-config-roundtrip.sh`/`disabled-precompile-call.sh`/`proxy-upgrade.sh`/`extcodehash-extcodecopy.sh`/`kzg-precompile-call.sh`/`gate-g3.sh`/`gate-g4.sh`;**gate-g3/g4 明确标注 "acceptance blocked on A8 op-node wiring" / skip** —— 从 CI 侧证实 §5.7 的 op-node 缺口 |

### 4.5 关键事实
- op-e2e **不接 op-node**:自有 PBFT 单节点共识当出块器(deposit 由节点每块注入),无 L1 mock、无真实 L1→L2 派生(`setup_op_node.sh` 克隆 optimism monorepo 仅用于 vendor 11 个预部署合约产物,非 op-node 接线)。
- 回执扩展字段(opStackMeta 13 字段)已对齐 op-geth `MarshalReceipt`;`OpReceiptEncodeTest` 黄金字节对拍。
- **已知 Bridge mint/burn DIVERGENCE**(预部署 bridge 未初始化)——§5.6 升级为跟踪项。

---

## 5. 逐维度缺口分析(以"FISCO 作为 OP-node EL"为目标;含审查修正)

### 5.1 EVM 核心语义
| 端 | 覆盖 |
|---|---|
| op-geth | EF 全量语料 + core 单测 + 预编译单测(Fjord P256/Jovian 输入限制,审查 A 补) |
| op-reth | 依赖 revm(上游)+ op-revm;无 EF runner |
| FISCO | t8n 83+46 向量 + 112 EVM 单测(16 套件) |

- **评级:🟡 部分对齐**。FISCO 预编译矩阵覆盖比 op-reth 更全;但 **EF 全量语料未跑全**——注意 **EEST runner 已存在且 12-fixture 冒烟 100% 过**(审查 C),所以不是从零,是**把冒烟子集扩到全量**。
- 缺口 = EF 全量语料执行(扩 EEST)。

### 5.2 OP fork 语义(deposit/L1-info/DA/fee)
| 端 | 覆盖 |
|---|---|
| op-geth | `rollup_cost_test.go` 全 fork L1 cost + `receipt_opstack_test.go` 回执全 fork + `op-e2e/opgeth` 各 fork deposit 语义 + eip4844 OPStack blob fee |
| op-reth | `l1.rs` 真实主网块解析 + consensus blob_gas_used/min_base_fee + **op-revm fee 数学单测**(审查 B 修正:与 op-geth 等价) |
| FISCO | DA 矩阵 16×3(四端逐位一致)+ t8n deposit 向量 + OpReceiptEncode |

- **评级:🟡 部分对齐**。DA/fee 对拍 FISCO **强项**(>= 两端);**缺口 = 历史 fork 的 deposit 专用语义向量(Regolith/Canyon/Ecotone/Fjord)** —— op-geth op-e2e/opgeth 整组,易迁移转 t8n。

### 5.3 区块构造 & 引擎 API(审查 D 重点纠正)
| 端 | 覆盖 |
|---|---|
| op-geth | `eth/catalyst` payload/attributes 校验 + FCU 空 attributes + `op-e2e` 无效 deposit 走 FCU + `miner/payload_building_test.go` 确定性 payload ID(审查 A 补) |
| op-reth | `node/engine.rs` attributes 校验 + payload builder(SDM/DA cap)+ **`PayloadId` 与 op-geth 逐字节一致** |
| FISCO | `a1_active.py` 16:FCU **V4** 全流程 + 篡改三字段拒绝 + V1-V3 版本门 + 未知 head SYNCING |

- **⚠️ 关键纠正(审查 D,已对照 `op-node/rollup/types.go`)**:op-node 实际发送的引擎方法版本是 —— **FCU:V1(Pre-Canyon)/V2(Canyon)/V3(Ecotone+,Isthmus+ 也是 V3)**;**NewPayload:V2/V3(Isthmus 用 V4)**;**GetPayload:V2/V3/V4(Isthmus)/V5(Karst)**。**不存在 FCU V4**。FISCO `a1_active.py` 主路径的 FCU V4 是 op-node 永不调用的方法 → 主测试路径错位。
- **评级:🟡→❌(相对 EL 目标)**,缺口(需镜像 `eth/catalyst/api_optimism.go` 校验规则):
  1. **引擎方法版本矩阵**(Canyon=E2/FCU V2;Ecotone=FCU V3+NP V3;Isthmus=FCU V3+NP V4+GP V4)——FISCO 需补 FCU V3 流程;
  2. **attributes 校验**:gasLimit **必需**(op-geth 对 nil 报错,`a1_active` attrs 省略了它)、Canyon 后空 withdrawals、Holocene EIP1559Params、Jovian MinBaseFee;
  3. **payload 校验**:Isthmus withdrawalsRoot nil/non-nil、Jovian blobGasUsed、Isthmus 后 requestsHash=EmptyRequestsHash、Holocene extraData eip1559 参数;
  4. **forkchoice 状态机**:latestValidHash、VALID/INVALID/SYNCING/ACCEPTED、未知父→SYNCING 再回填、FCU head 变更 reorg(现只覆盖"未知 head→SYNCING"+3 个篡改向量);
  5. **payload ID 确定性**:同 attrs → 与 op-geth 同 payload ID/块哈希(op-reth 有字节级 parity,op-geth 有 `TestDeterministicPayloadId`)。

### 5.4 RPC 层(审查 C+D 修正)
| 端 | 覆盖 |
|---|---|
| op-geth | eth_*/ethclient/gasprice/feehistory/rpc 各包 + deposit tx RPC 转换(`internal/ethapi`) |
| op-reth | rpc 20(回执扩展字段真实主网、bedrock 转发中间件 8、pending block) |
| FISCO | rpc_matrix 45 + predeploy_matrix(eth_call/sendRawTransaction)+ 回执 OP 扩展字段 + **bcos-rpc ~140 单测(含 EngineRpc/Web3Response/BlockTag)** |

- **修正(审查 C)**:`eth_getProof` **端点已注册且有 14 个单测**(OP 模式 `feature_l2_ethereum_compat`),仅 op-e2e 未覆盖 → 不是"未见",是"缺 e2e 断言";历史 tag **可路由**(callAtBlock),缺的是"honoring 块 N 状态"。
- **真实缺口(审查 D,对照 op-node 硬依赖)**:
  1. **`eth_getBlockByNumber/Hash(fullTx=true)` 的 deposit tx round-trip**:op-node `derive.PayloadToBlockRef`/`PayloadToSystemConfig` 把 `tx[0]`(类型 0x7E)解析成 block ref 与 SystemConfig —— **这是 op-node 每块第一件事,spec 初版完全没列为契约项**;
  2. **`eth_getProof` 升 P1**:在 op-node `L2EthClient` 接口内,预 Isthmus output root(op-proposer `OutputV0AtBlock`)依赖它;
  3. **safe/finalized block tag**:op-node `L2BlockRefByLabel` 用 `eth_getBlockByNumber("safe"/"finalized")`;
  4. **`eth_feeHistory`/`eth_protocolVersion`**:rpc_matrix 是 declared-gap,spec 初版漏列;
  5. **历史 `eth_call` 静默返 `0x`**:rpc_matrix 注释承认,比"响亮拒绝"更危险(审查 D);
  6. **debug_* 归因修正**:`debug_getRawReceipts` 是 op-node 对 **L1** 客户端的依赖,非 L2 EL;L2 侧 `debug_executionWitness` 属于 **fault proof(op-program)**,应挂在 fault-proof 范围,不是 op-node 核心派生路径。
- **评级:🟡 部分对齐**。缺 gasPrice 真实现、历史状态 honoring、getProof e2e 断言、deposit round-trip、feeHistory/protocolVersion、safe/finalized。

### 5.5 状态/存储/MPT/reorg/同步(审查 C+D 修正)
| 端 | 覆盖 |
|---|---|
| op-geth | statedb/trie_prefetcher/statedb_fuzz + blockchain reorg/repair/snapshot |
| op-reth | trie crate 333(上游)+ Go e2e reorg/prune/proof |
| FISCO | **MPT/trie/proof 147 用例**(审查 C 补,spec 初版漏)+ state_verify 12 + Storage2StateHelpers + EngineServiceTest 的 FCU forkchoice 顺序测试(reorg 相邻) |

- **修正(审查 C)**:§5.5 初版"❌ 缺失较多"**夸大**——MPT/trie/proof 套件 147 用例真实存在,历史归档确实无,但"MPT 缺失"不成立。
- **真实缺口**:
  1. **reorg 执行测试**:无完整链级 reorg(仅 FCU forkchoice 顺序单测);
  2. **历史归档(MPT archive)**:无 → 历史 tag 状态 honoring 走不了;
  3. **EL 状态同步(审查 D,全新维度)**:FISCO 作为新 EL 接入现存链时,op-node 会下发父块缺失的 payload → EL 须返 SYNCING 并**回填状态**;FISCO 无 P2P 状态同步(snap-sync 等价物)——**这是 EL 替代的操作性硬阻断,不是测试细节**。
- **评级:🟡**(MPT 基座强,但缺 reorg 执行 + 归档 + 状态同步)。

### 5.6 预部署/系统合约
| 端 | 覆盖 |
|---|---|
| op-geth | op-e2e Go 流程 + Create2Deployer 单测(审查 A 补) |
| op-reth | 极薄(仅 MessagePasser withdrawalsRoot 空态 + L1Block 间接) |
| FISCO | **predeploy_matrix 30 断言(5 合约)+ t8n L1Block/MessagePasser 向量 + bcos-l2-contracts forge 测试** |

- **评级:✅ FISCO 领先**。**但(审查 D)已知 Bridge mint/burn DIVERGENCE 必须升级为跟踪项** —— 它是跨域兼容风险,直接削弱"预部署矩阵是强项"的声明,应 P1/P2 修复,不能只当脚注。

### 5.7 横切:op-node 集成 / fuzz / 新特性(审查 C+D)
| 项 | op-geth | op-reth | FISCO |
|---|---|---|---|
| **op-node 驱动 EL** | ✅ op-e2e 全栈 | ✅ tests/ Go e2e + node e2e | ❌ **无**(自有 PBFT,无 L1 mock);CI 门 `gate-g3/g4` 自述"blocked on A8 op-node wiring" |
| fuzz | ✅ tests/fuzzers(6 类) | 🟡 primitives proptest | ❌ 无 |
| SDM/post-exec `0x7d` | 🟡 | ✅ 强 | ❌ |
| flashblocks | — | ✅ 145 | ❌ |
| interop | ✅ 树内 miner/txpool + op-e2e | ✅ txpool/payload | ❌ |
| fault proof | ✅ op-e2e/faultproofs | ✅ tests/ proofs | ❌(无此系统) |
| superchain 信号 | ✅ catalyst + registry | 🟡 registry | ❌ |

- **评级:❌ op-node 集成为最大缺口**(审查 C 从 CI 门、审查 D 从 op-node 契约双重证实)。

---

## 6. 结论:FISCO 已有哪些 / 缺哪些(修订版)

### ✅ FISCO 已具备且相对两端不弱(甚至领先)
1. **DA/operator fee 共识对拍**:DA 矩阵 16×3 四端逐位一致 + BcosEvmOpstack 的 RollupCost/OpFeeParams 套件 —— 与 op-geth `rollup_cost_test.go`、op-revm fee 单测同源同强度。
2. **OP 回执推导**:`OpReceiptEncodeTest` 黄金字节 + opStackMeta 13 字段对齐 op-geth `MarshalReceipt`;bcos-rpc `Web3ResponseTest` 覆盖 RPC 层扩展字段。
3. **预部署合约行为矩阵**:predeploy_matrix 30 断言三端中最系统;bcos-l2-contracts forge 测试补合约源码层。
4. **预编译覆盖 + t8n 差分基建**:83 正向 + 46 负向向量,锚定 op-geth,易扩展。
5. **引擎/RPC 单测基座**(审查 C 补):bcos-rpc ~140(EngineRpc V1-V4/Web3Type/BlockTag)、eth_getProof 14、MPT/trie/proof 147 —— 初版严重低估。

### 🟡 部分对齐(需补强)
6. **历史 fork deposit 语义**(Regolith/Canyon/Ecotone/Fjord):op-geth op-e2e/opgeth 整组缺失 → 易迁移,转 t8n 向量。
7. **引擎 API 契约**(审查 D):方法版本矩阵错位(FCU V3 非 V4)、attributes/payload 校验规则(gasLimit/withdrawals/EIP1559Params/MinBaseFee/withdrawalsRoot/blobGasUsed/requestsHash)、forkchoice 状态机、payload ID 确定性。
8. **RPC 负载路径**(审查 D):`eth_getBlockBy*` deposit tx round-trip、eth_getProof e2e、safe/finalized、feeHistory/protocolVersion、gasPrice 真实现。
9. **历史状态 honoring**:归档缺失 → 历史 tag 只能"响亮拒绝"或静默 `0x`(后者是正确性隐患)。

### ❌ 完全缺失(EL 替代的硬缺口)
10. **op-node 集成测试**:最大缺口。无 L1 mock、无真实 L1→L2 deposit 派生、无 op-node 驱动出块;CI 门自述 blocked。
11. **EL 状态同步**:接入现存链时的 SYNCING→回填(审查 D 新维度,操作性硬阻断)。
12. **reorg 执行测试**:仅 forkchoice 顺序单测,无链级 reorg。
13. **fuzz / SDM / flashblocks / interop / fault proof**:全缺(fault-proof/interop 属架构差异,fault proof 需明确是否入范围)。

---

## 7. 建议迁移优先级(审查 D 修订,针对"OP-node EL 替代"目标)

| 优先级 | 迁移项 | 来源 | 落点 / 说明 |
|---|---|---|---|
| **P0** | **op-node 集成 harness + 引擎 API 契约合规矩阵** | op-geth `op-e2e`(Go 全栈)+ `eth/catalyst/api_optimism.go` | L1 mock → op-node 派生(L1-info tx[0]+deposit+fork upgrade txs)→ FCU(**V3**)/getPayload/newPayload → 断言块哈希与 op-geth 逐字节一致;harness 须拆到组件级(见 §7.1) |
| **P0** | **`eth_getBlockBy*` deposit tx round-trip** | op-node `derive.PayloadToBlockRef/SystemConfig` | 用真实 `derive` Go 代码或复刻 harness 跑 FISCO 的块,断言 tx[0] 是 0x7E 且 JSON round-trip |
| **P0-并行** | **EF 全量语料执行**(扩 EEST) | ethereum/tests + execution-spec-tests | **非从零**:EEST runner 已存在且 12-fixture 冒烟 100% 过,扩到全量 |
| **P1** | **`eth_getProof` 补 e2e + OutputV0 合规** | op-node `OutputV0AtBlock` 路径 | 单测已有 14 例,补 op-e2e 断言 + 重放 op-node 的 GetProof(L2ToL1MessagePasser,[]) → Verify → MessagePasserStorageRoot |
| **P1** | **历史 fork deposit 语义向量** | op-geth `op-e2e/opgeth/op_geth_test.go`(Preregolith→Isthmus) | 转 t8n 向量 |
| **P1** | **引擎 API 边界**:FCU V3 流程 + FCU 空 attributes + 无效 deposit 走 FCU + attributes/payload 校验规则 | op-geth `eth/catalyst` + `opgeth` | 修 `a1_active.py` 主路径 + 补校验向量 |
| **P2** | **RPC 补齐**:eth_gasPrice、feeHistory、protocolVersion、safe/finalized、历史状态 honoring(归档或明确拒绝) | op-geth `gasprice` / op-reth `rpc` | rpc_matrix 补断言 + 实现 |
| **P2** | **reorg 执行测试 + EL 状态同步** | op-reth `tests/proofs/reorg_test.go` | 需存储层支持;状态同步是独立工作流 |
| **P2** | **Bridge DIVERGENCE 修复**(跟踪项) | — | 跨域兼容风险,须修复不能只登记 |
| **P3** | **SDM/post-exec `0x7d` / debug_executionWitness** | op-reth `post-exec-replay`/`payload`;fault-proof 范围 | 若 fault proof 入范围则 witness 升 P1 |

### 7.1 P0 harness 必须拆到的组件级(审查 D)
(a) L1 mock —— geth devnet 或 op-e2e L1 fixture,带真实 `OptimismPortal` 发 `DepositEvent`;
(b) L1 批量数据 —— op-batcher 或预播种;
(c) op-node 派生 —— attrs 含 **L1-info tx + deposits + fork 升级 tx**(`EcotoneNetworkUpgradeTransactions`/Fjord/Isthmus/Jovian,`op-node/rollup/derive/attributes.go`);
(d) FCU→getPayload→newPayload→FCU 驱动 FISCO;
(e) 确定性门 —— 同 attrs 下 FISCO 块哈希与 op-geth 逐字节一致;
(f) deposit→L2 与 withdraw→MessagePasser→`eth_getProof`→finalize 端到端;
(g) 重启与 reorg 韧性。
最简路线:复用 optimism monorepo `op-e2e/actions` 框架,把 L2 EL 从 op-geth 换成 FISCO。

---

## 8. 附:三端关键文件索引(便于核对)

- FISCO:t8n `opstack-executor/tests/t8n/{cases,vectors,generator,golden}`、op-e2e `tools/op-e2e/`、回执 `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp`、meta `bcos-framework/bcos-framework/protocol/TransactionReceipt.h:37`、RPC 单测 `bcos-rpc/test/unittests/rpc/`、MPT `bcos-ledger/test/unittests/mpt/`、EEST `ethereum-executor/` + `bcos-evm/test/eth-eest-test/`、CI 门 `tools/.ci/l2-integration/`。
- op-geth:OP 专属 `core/types/{rollup_cost,receipt_opstack,deposit}_test.go`、`eth/catalyst/api_optimism_test.go`、`core/superchain_test.go`、`superchain/superchain_test.go`、`miner/{miner_optimism,op_interop_miner,payload_building}_test.go`、`internal/ethapi/api_test.go`、EF runner `tests/{state,block,transaction}_test.go`。
- op-reth:共识 `crates/consensus/src/{lib.rs,proof.rs,validation/isthmus.rs}`、L1-info `crates/evm/src/l1.rs`、回执 `crates/rpc/src/eth/receipt.rs`、向量 `op-test-vectors/evm/`、builder `crates/payload/src/builder/tests.rs`、fee 数学 **`op-revm/src/{l1block.rs,fast_lz.rs}`**、exex `crates/exex/src/lib.rs`。
