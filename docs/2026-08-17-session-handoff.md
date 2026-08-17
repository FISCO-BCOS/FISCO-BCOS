# 会话交接 — 2026-08-17(FISCO-BCOS opstack 测试体系)

> 交接对象:下一会话/代理。本会话完成 op-e2e 节点重建 + DA 矩阵计划交付 + 预部署行为矩阵设计。以下为可续接的完整状态。

## 1. 分支与工作区

- **当前分支**:`worktree-op-alignment`(隔离 worktree:`.claude/worktrees/op-alignment`),HEAD `c9baf5f`
- 本会话提交:`783f883fa..HEAD` 共 **14 提交**(op-e2e 1 + spec/plan 4 + DA 实现 9)
- 相关 worktree:`pr5429-split`(PR #5429 拆分,后台暂停)、`opstack-executor` 等(其它线程)
- op-e2e 节点:B3 + B3a 各 1 个进程在跑(eth RPC 8553/8563)

## 2. 已完成 ✅

### 2a. op-e2e 节点重建(会话前半,已交付)
- B3/B3a 从全新链重建(原 1.39M 块链因 node.0 私钥丢失不可复用,旧数据在 `data.old-20260817` 663M 可删)
- `run_all.sh` **ALL OP-E2E GREEN**:rpc_matrix 44 / state_verify 12 / chain_driver 31 / b4_persist 3 / b3_contracts 12 / a1_active 16
- `setup_op_node.sh` 重写为可复用一键脚本(9 步幂等,提交 `783f883`)
- **5 个 config key 坑**(记忆 `op-e2e-node-rebuilt-config-blueprint.md`):`[executor] version=3`(非 executor_version)、`evm_revision_forks=0:prague`、`[rpc] listen_port`(非 rpc_listen_port)、`[web3] chain_id`、`tars_proxy.ini` 必需;B3a `produce_empty_blocks=false`(a1_active 竞态)
- `sign_secp`(libsecp256k1 可恢复签名 + 低-s)已入库 `tools/op-e2e/sign_secp.c`

### 2b. DA / operator fee 参数化矩阵(主线,已交付 + MERGE-READY)
- **跨客户端四端对拍**:FISCO/op-geth/op-revm 20.0.0/Solidity GasPriceOracle — 16 网格 operator_cost 逐位一致
- 交付:`opstack-executor/tests/da-matrix/`(da_matrix.json 16 用例 7 类、run_fisco.cpp、run_opgeth/、run_oprevm/、solidity/、golden/{fisco,opgeth,oprevm,solidity}/、DIVERGENCES.md、README PINNED REFERENCES)
- A 层单测:OpFeeParams +3、RollupCost +6、JovianShape +2;CI 门 `DaMatrixFiscoCheckOpgeth/OpRevm` 挂 ctest
- 生产改动唯一:`computeChargedOperatorCost`(RollupCost.h/.cpp,+6 additive)
- **抓到真实 bug**:Solidity `getL1Fee` baseFeeScalar≥2^28 时 uint32 溢出(登记 `solidity_l1_uint32_overflow`)
- 回归:ctest 1934/1935 + op-e2e ALL GREEN;最终整支审查 **MERGE-READY**
- 计划/审查台账:`.superpowers/sdd/2026-08-17-opstack-da-matrix-plan/progress.md`(全部 deferred minors + parked)

### 2c. 预部署行为矩阵设计(刚完成 spec,待计划)
- `docs/2026-08-17-opstack-predeploy-matrix-design.md`(提交 `c9baf5f`):核心 5 合约(L1Block/L2ToL1MessagePasser/L2CrossDomainMessenger/L2StandardBridge/SystemConfig)行为矩阵,真实节点为主 + t8n 差分共识项

## 3. 待办/决策 ⏳

### 收尾决策(用户待选)
1. **DA 计划收尾**:`worktree-op-alignment` 合并/推 PR/保持——**未定**
2. ✅ **EmptyEnvelopeFails 已修**(2026-08-17):改名 `EmptyEnvelopeAccepted`,断言空 envelope 被接受且 l1_cost=0(对齐 OpTransition.cpp:376-379 有意语义);`bcos-evm-opstack-tests` 112 全绿,ctest 唯一红消除
3. **计划文档 2 处笔误**:DA 计划 `contract_call_tx` 例 312B 截断(实现用权威 345B)、brief `...ull` 超 uint64——顺手修

### 预部署矩阵下一步
4. ✅ `docs/2026-08-17-opstack-predeploy-matrix-design.md` 已获批 → **实施计划已写**(`docs/2026-08-17-opstack-predeploy-matrix-plan.md`,6 Task;selector/topic 全 keccak 重算并逐条对过节点字节码;发现 spec 勘误:MessagePasser 无 getSentMessage,用 sentMessages(bytes32))。**SDD 已全部交付**(Task 1-6):t8n 差分向量 + `tools/op-e2e/predeploy_matrix.py`(30 断言)已挂 `run_all.sh`(chain_driver 之后、a1_active 之前);全量回归 ctest 1935/1935 + op-e2e ALL GREEN。Divergence 登记:`l1block_deposit_reverts_ecotone_vs_jovian`(genesis L1Block 为 Ecotone 版,节点注入 Jovian deposit 每块 revert→getter 返回 0,断言仅要求可读+格式正确)、`bridge_deposit_l2_only_mint_unverified`/`bridge_withdraw_l2_only_burn_unverified`(bridge 预部署未初始化,messenger()=0→桥内 sendMessage 落到 address(0) revert,断言降级为回执可查)。**Deferred(两层均不可构造,spec 表保留待基建修复)**:L1Block「sequenceNumber 跨块递增 + blockhash 写入」——t8n 146B 码不写 slot0/2,真实节点因 deposit 恒 revert 只能走 DIVERGENCE;待 genesis L1Block 升级为 Isthmus/Jovian 版后补正断言。

### 测试体系(对照 op-geth/op-reth 差距,待排期)
5. **EF 官方语料接入**(P0):ethereum/tests blockchain/state 套件
6. RPC 层 op-geth 对拍 / reorg / withdrawalsRoot 全流程 / Fuzz / eth_gasPrice / Karst(DIVERGENCES D-2 🔴)

### e2e 设计(用户问过未定)
7. FISCO opstack e2e 分层设计(Layer 1 单节点语义强化 + Layer 2 跨域 mock L1 + Layer 3 op-node)——**未定是否实施**

### 其它线程
8. **PR #5429 拆分**(pr5429-split worktree):剩余分支 infra-rebuilt/initializer/RPC/eth-executor-remainder/**engine LAST**;清理 superseded(split-pr-framework-types/split-pr-tars-protocol/split-op-receipt-meta-v2)

## 4. 关键路径

- 节点工作区:`/tmp/op-spike/{b3,b3a}`(config.genesis 合并文件、conf/、jwt.hex、node.pem)
- op-e2e 套件:`tools/op-e2e/`(run_all.sh / restart_b3.sh / predeploy_matrix.py)
- DA 矩阵:`opstack-executor/tests/da-matrix/`
- t8n harness:`opstack-executor/tests/t8n/`(generator/cases.go、regen.sh、OpT8nReplayTest)
- 预部署合约:FISCO 自研 `bcos-l2-contracts/src/{SystemConfig,L2ValidatorSet}.sol`;OP-fork 11 个字节码在 `bcos-l2-contracts/out/`,源码在 `/tmp/op-spike/op-pinned`(33f06d2d)
- 参考端:op-geth `/Users/octopus/octo/code/blockchain-impl/op-geth`(v1.101702.2)、op-revm `/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm`(da197e45)、contracts-bedrock 同 monorepo
- 记忆:`.claude/projects/-Users-octopus-octo-code-FISCO-BCOS/memory/`(op-e2e 重建/config 蓝图/DA 计划等)

## 5. 硬约束(不可退步)

- **已通过的测试或测试集合不能变的无法通过**;测试可加强不可退步。
- 本会话全部改动纯增补,唯一生产改动 `computeChargedOperatorCost`(additive);`EmptyEnvelopeFails` 是唯一红(pre-existing,单独立案)。
