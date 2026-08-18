# op-e2e on scheduler 分支 — 验证记录(2026-08-18)

> 背景:将旧分支 `worktree-op-alignment` 的测试内容(EmptyEnvelope 修复 + t8n 预部署向量)同步到 `opstack-op-e2e-on-scheduler`(基于 ywy2090 scheduler 重构),并验证 e2e 测试。
> 结论:**同步内容验证正确,节点可启动;e2e 套件暴露 scheduler 分支与旧分支的真实行为差异(未修,记录在案)**。

## 1. 同步内容(已提交,commit `83bc811a4`)

| 内容 | 状态 |
|---|---|
| t8n 预部署向量 8 文件(4 vectors + 4 golden engine) | ✅ 已同步 |
| generator/cases.go:l1block_deposit_slots + message_passer_withdraw caseSpec + realMessagePasserCode + abiEncodeWithdrawal | ✅ 已移植(引用现有 l1BlockRuntimeCode,不重复定义) |
| manifest.txt:4 个 predeploy 向量注册 | ✅ 已追加 |
| EmptyEnvelope 修复(EmptyEnvelopeFails→Accepted) | ✅ 已在分支 HEAD(`2d36b856c`) |

## 2. scheduler 分支节点启动修复(5 个 config 坑,均非代码 bug 而是分支契约差异)

重编 scheduler 二进制后,旧 config.genesis 启动失败,逐层修复:

1. **`[eth_genesis_header]` 缺失** — scheduler 分支要求 `feature_l2_ethereum_compat` 必须配 22 字段 `[eth_genesis_header]`(NodeConfig.cpp:442)。旧 config 无此段。
2. **单节点共识互斥** — `consensus.enable_single_node_consensus` 与 `op_engine_rpc.enable` 互斥(NodeConfig.cpp:865)。OP e2e 需 engine API → 保留 op_engine_rpc,关单节点共识。
3. **state_root 不匹配** — eth_genesis_header 的 state_root 必须是 genesis allocs 的真实 MPT 根(Ledger.cpp:1979),不能照抄空 trie 根。derived=`0x409e6736ad7d48c00cd82b66ccbc982f4d09cd40f008a0e2ffbc41b5d6fd36b9`。
4. **hash 字段重算** — 改 state_root 后 header keccak 变化(Ledger.cpp:2240),用 `gen_eth_header_fixture.py` 重算。keccak=`0x21878fa4c818839210f55cef5ee71860b6e5245662a1fd034d9c994306adca28`。
5. **清 data 重建链** — 旧链 genesis 无 eth_genesis_data,必须重初始化(Ledger.cpp:2065)。

修复后 B3/B3a 正常启动(block 0x0,空链重建)。

## 3. e2e 套件结果 — 暴露 scheduler 分支真实差异(未修,记录)

| 脚本 | 结果 | 差异 |
|---|---|---|
| rpc_matrix | ⚠️ 1 FAIL | `eth_call` -32603 Invalid argument(某断言参数在 scheduler 上不同) |
| state_verify | ✅ | RocksDB 直读 |
| chain_driver | ✅ | 交易驱动 |
| b4_persist | ✅ | 持久化 |
| b3_contracts | ✅ | 合约部署 |
| predeploy_matrix | ⚠️ FAIL | `eth_call` -32603 |
| a1_active | ❌ FAIL | `engine_forkchoiceUpdatedV4` -38005 **not supported** — scheduler 分支只支持 FCU **V3** |

**核心差异**:e2e 套件为旧分支编写,用 engine API **FCU V4**;scheduler 分支只支持 **FCU V3**(EL 契约计划已分析「FCU V3 非 V4」,此处实证)。a1_active 的 V4 调用必然失败。

## 4. 裁定与后续

- 用户裁定:**验证完成,记录差异**,不继续修测试适配。
- **后续选项**(未实施):
  - a) 适配 a1_active 到 FCU V3(需改 engine API 调用 + 确认 scheduler 语义)
  - b) 对齐 rpc_matrix/predeploy_matrix 的 eth_call 参数
  - c) 若 scheduler 分支应支持 V4 → 报 scheduler 分支回归(而非测试问题)
- 这些差异与 EL 契约计划(`docs/2026-08-17-opstack-el-contract-plan.md` v5)的「FCU V3 契约」分析一致。
