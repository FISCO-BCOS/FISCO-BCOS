# 会话交接 — 2026-08-18(FISCO-BCOS opstack 测试体系)

> 交接对象:下一会话/代理。本会话完成 op-e2e 节点重建 + DA 矩阵 + 预部署矩阵 + **e2e CI 接入 + MPT state_root + 回归修复(全绿)**。以下为可续接的完整状态。

## 1. 分支与工作区

- **当前分支**:`worktree-op-alignment`(隔离 worktree:`.claude/worktrees/op-alignment`),HEAD `84b3be0`
- 相关分支:`opstack-op-e2e-on-scheduler`(基于远程 tip `174d8e4c2`,被停代理自主创建,用户裁定保留;含 EmptyEnvelope 修复 + 2 文档 + 可配置化 + CI job;**genesis 链路断裂待补,见 §3-10**)
- op-e2e 节点:本地 `/tmp/op-spike/{b3,b3a}`(本会话从零重建,ALL GREEN 149 断言)
- ~~未跟踪文件:`tools/opstack-genesis/chain-config.yaml`~~ ✅已处置(08-18,A2):经比对与已入库的 `chain-config.template.yaml` 逐字节相同,系 `setup_op_node.sh:141` 幂等复制的本地运行时产物(README 约定 cp template 后本地编辑)——**不入库**,已加 `.gitignore`(本地文件保留)

## 2. 已完成 ✅

### 2a. op-e2e 节点重建(08-17,已交付)
- B3/B3a 从全新链重建(原 1.39M 块链因 node.0 私钥丢失不可复用,旧数据在 `data.old-20260817` 663M 可删)
- `setup_op_node.sh` 重写为可复用一键脚本(9 步幂等,提交 `783f883`)
- **5 个 config key 坑**(记忆 `op-e2e-node-rebuilt-config-blueprint.md`):`[executor] version=3`、`evm_revision_forks=0:prague`、`[rpc] listen_port`、`[web3] chain_id`、`tars_proxy.ini` 必需;B3a `produce_empty_blocks=false`
- `sign_secp`(libsecp256k1 可恢复签名 + 低-s)已入库 `tools/op-e2e/sign_secp.c`

### 2b. DA / operator fee 参数化矩阵(已交付 + MERGE-READY)
- **跨客户端四端对拍**:FISCO/op-geth/op-revm 20.0.0/Solidity GasPriceOracle — 16 网格 operator_cost 逐位一致
- 交付:`opstack-executor/tests/da-matrix/`(da_matrix.json 16 用例 7 类、run_fisco.cpp、run_opgeth/、run_oprevm/、solidity/、golden/、DIVERGENCES.md、README PINNED REFERENCES)
- 生产改动唯一:`computeChargedOperatorCost`(RollupCost.h/.cpp,+6 additive);抓到 Solidity uint32 溢出 bug
- 回归:ctest 1934/1935 + op-e2e ALL GREEN;MERGE-READY;台账 `.superpowers/sdd/2026-08-17-opstack-da-matrix-plan/progress.md`

### 2c. 预部署行为矩阵(已交付)
- spec + plan,SDD 6 Task 全绿;t8n 差分向量 + `predeploy_matrix.py` 30 断言挂 run_all;ctest 1935/1935 + op-e2e ALL GREEN;MERGE-READY

### 2d. e2e CI 接入 + MPT state_root + 回归修复(2026-08-18,已交付全绿)
- **CI op-e2e job**:`workflow.yml` 追加(ubuntu-24.04 + macos-15 矩阵,装 foundry → 编译 → setup → run_all)
- **op-e2e 脚本可配置化**(`440a497`):硬编码 → env(SIGN_SECP/B3A_JWT/B3_JWT/B3_DB/OP_STATE_READ/B3A_START)
- **MPT state_root**(`c33bfe83f`):`mpt_state_root.py`(纯 Python op-geth 兼容 secure MPT,逐字节 == C++ `GenesisStateRoot.cpp`)+ `gen_eth_header_fixture.py --allocs` + setup 集成。验证:`0x0f4dbf6c...` == C++ derived;`eth_getBlockByNumber(0)` stateRoot/hash 四者一致。报告 `docs/2026-08-18-mpt-root-report.md`
- **回归修复**(`84b3be0`):`enable_single_node_consensus=true` 恢复(见 §3-4c 详解)
- **scheduler 分支验证**:`docs/2026-08-18-opstack-scheduler-e2e-verification.md`(5 config 修复 + FCU V3 vs V4 差异)

### 2f. B1/B3 RPC 对拍(2026-08-18,已交付全绿)
- **B1 eth_gasPrice 对拍**:op-geth = `head.baseFee + tip`(≥1e6,本地算不查 GPO);FISCO = ledger 配置(恒 0x0)。分歧 D-GP-1/2/3 登记 + 两侧行为钉死(C++ 16/16 + 活链);对齐需生产改动,单独立案
- **B3 withdrawalsRoot 全流程**:四层一致证据——op-geth golden(既有)→ C++ seal(含新增空态常数用例)→ Python trie 复算 golden `0x02dffd0c…` → 活链 seal↔RPC↔getProof。实测锚定 sentMessages slot0 = keccak(wh‖0);创世 getProof storageRoot == `0x56e81f…b421` == EmptyWithdrawalsHash
- **新分歧登记**:D-WR-1/2(JSON 键形状)、D-WR-3(创世头缺 withdrawalsRoot 字段,单独立案)、getProof 仅创世可用(MPT 节点只在 genesis 导入写入)
- 报告 `docs/2026-08-18-rpc-parity-gasprice-withdrawals.md`;工具解耦:build-allocs yaml 延迟到 CLI,mpt_state_root 可无 yaml 导入
- **回归**:run_all ALL GREEN **160 断言**(149→+11:rpc 45→51、predeploy 30→35);opstack-executor-block-tests 137 用例绿;run_all.sh 头部计数同步

### 2e. 存档(被停代理产出,用户裁定保留,非经批准交付物)
- `docs/2026-08-17-opstack-testmatrix-compare-design.md`(三端对比 spec)
- `docs/2026-08-17-opstack-el-contract-plan.md`(EL 契约计划 v5 终版)
- `opstack-op-e2e-on-scheduler` 分支

## 3. 待办/决策 ⏳

### 收尾决策(用户待选)
1. **DA 计划收尾**:`worktree-op-alignment` 合并/推 PR/保持——**未定**
2. ✅ **EmptyEnvelopeFails 已修**(08-17):改名 `EmptyEnvelopeAccepted`
3. ✅ **计划文档 2 处笔误已修**(08-18):DA 计划 `contract_call_tx` hex 替换为与 da_matrix.json/RollupCostTest.cpp 逐字节一致的 345B 权威版(原 312B 系转写丢 3 段零);brief/plan 的 `...ull` 超 uint64 字面量改 `_u256`(与实现一致);progress.md 台账同步

### e2e CI(本会话主线,已全绿,剩收尾)
4a. ✅ CI op-e2e job + 脚本可配置化(`440a497`)
4b. ✅ eth_genesis_header 生成(`3ea2285` 初版;⚠️ 该 commit 含一处误改,已由 `84b3be0` 修复)
4c. ✅ **MPT state_root**(`c33bfe83f`)+ **enable_single_node_consensus 回归修复**(`84b3be0`)
    - **回归详情**:`3ea22859e` 把 `enable_single_node_consensus=true` 改 `false`,声称"与 op_engine_rpc 冲突"——**误判**。false 下 B3 链停摆(不出块,blockNumber 恒 0),chain_driver/rpc_matrix/b4_persist/b3_contracts/predeploy 全红。改回 true 后**从零重建全绿 149 断言**,且 a1_active(B3a,FCU 驱动)16/16 证明 true+op_engine_rpc 可共存。**记忆**:`op-e2e-single-node-consensus-regression.md`
    - **子代理误判记录**:子代理(ab2661bd)报 run_all 未全绿并归因"V4 端点是桩/既有问题"。实为 worktree build 陈旧 librpc.a(08-11 对象,含桩串);强制重建后桩串 3→0,worktree 源码是真实 V4(`514e87046`)。**记忆**:`stale-worktree-build-v4-stub-trap.md`
    - **当前验证**:本地 `setup_op_node.sh` 从零重建 → **ALL OP-E2E GREEN**:rpc_matrix 45 / state_verify 12 / chain_driver 31 / b4_persist 3 / b3_contracts 12 / predeploy 30 / a1_active 16 = **149 断言**
4d. ⏳ scheduler 分支 Karst base-allocs 缺口(task 104):`base_allocs_sha256=""`,需 op-deployer 生成 terminal allocs。**非当前分支必需**(worktree-op-alignment genesis 链路完整)

### 测试体系(对照 op-geth/op-reth 差距,待排期)
5. **EF 官方语料接入**(P0):ethereum/tests blockchain/state 套件(扩现有 EEST runner)
6. RPC 层 op-geth 对拍 / reorg / withdrawalsRoot 全流程 / Fuzz / eth_gasPrice / Karst(DIVERGENCES D-2 🔴)——✅ **eth_gasPrice + withdrawalsRoot 已完成**(08-18,§2f);✅ **B4: P1 三件套完成**(08-18,getProof e2e + engine API boundary; pre-Isthmus 向量已覆盖);剩 Fuzz(2 天+)/ Karst D-2(单独立案)
7. **三端测试对比 spec(存档,非用户请求)**:`docs/2026-08-17-opstack-testmatrix-compare-design.md`。核心结论:最大硬缺口 = **op-node 集成 harness + 引擎 API V3 版本契约**。可作排期输入
8. **op-node EL 契约实施计划(存档,v5 终版)**:`docs/2026-08-17-opstack-el-contract-plan.md`。瞄准生产改造,**须重新立项,不得直接执行**
9. **reorg 能力实现(08-18 重分类,D4)**:**FISCO opstack 当前没有链级 reorg 能力**。testmatrix spec §5.5 评级 🟡(MPT 基座强,缺 reorg 执行+归档+状态同步)。原始估为"约 1 天测试增补"(B2),实际为生产特性——需要:①引擎 FCU safe/finalized head 切换语义(执行层撤销已提交块、恢复多层存储到旧状态)②存储层回退(MultiLayerStorage commit/mergeView 支持"丢弃最近 N 块写入")③RPC 读取一致性(被回退块的交易/receipt/状态不可查或标记 reverted)。与 D2(EL 契约)耦合——op-node 驱动 reorg 的通道(FCU safe/finalized 切换)正是 D2 要实现的引擎 API 契约一部分;建议合并立项。
10. **`opstack-op-e2e-on-scheduler` 分支(保留)**:2 commits(`1d251f040` op-e2e 套件 + `f01285ed0` DA 矩阵),基于远程重构;计划 v5 行号在其上不成立
11. **scheduler 分支后续处置**:①已补 EmptyEnvelope 修复(`2d36b85`)②t8n 预部署向量不需同步 ③**genesis 链路断裂**:`op-fork-pin.toml` `[karst_pin] base_allocs_sha256=""`,需按 pin 生成 allocs 并填 sha256(单独立项)④当前 e2e 验证在旧分支进行

### e2e 设计(用户问过未定)
7. FISCO opstack e2e 分层设计(Layer 1 单节点语义强化 + Layer 2 跨域 mock L1 + Layer 3 op-node)——**未定是否实施**

### 其它线程
8. **PR #5429 拆分**(pr5429-split worktree):剩余 infra-rebuilt/initializer/RPC/eth-executor-remainder/**engine LAST**;清理 superseded 分支

## 4. 关键路径

- 节点工作区:本地 `/tmp/op-spike/{b3,b3a}`(ALL GREEN);CI 模拟 `/tmp/op-e2e-ci-sim{2,3}`
- op-e2e 套件:`tools/op-e2e/`(run_all.sh / restart_b3.sh / predeploy_matrix.py / setup_op_node.sh)
- genesis 工具:`tools/opstack-genesis/`(build-allocs.py / gen_eth_header_fixture.py --toml --allocs / mpt_state_root.py / chain-config.yaml 模板)
- CI:`.github/workflows/workflow.yml`(op-e2e job)
- DA 矩阵:`opstack-executor/tests/da-matrix/`
- t8n harness:`opstack-executor/tests/t8n/`(generator/cases.go、regen.sh、OpT8nReplayTest)
- 预部署合约:FISCO 自研 `bcos-l2-contracts/src/{SystemConfig,L2ValidatorSet}.sol`;OP-fork 11 字节码在 `bcos-l2-contracts/out/`,源码在 `/tmp/op-spike/op-pinned`
- 参考端:op-geth `/Users/octopus/octo/code/blockchain-impl/op-geth`(v1.101702.2)、op-revm `/Users/octopus/octo/code/blockchain-impl/optimism/rust/op-revm`(da197e45)、contracts-bedrock 同 monorepo
- 记忆:`.claude/projects/-Users-octopus-octo-code-FISCO-BCOS/memory/`(含本会话新增 `op-e2e-single-node-consensus-regression.md` + `stale-worktree-build-v4-stub-trap.md`)

## 5. 硬约束(不可退步)

- **已通过的测试或测试集合不能变的无法通过**;测试可加强不可退步。
- 本会话全部改动纯增补;唯一生产改动 `computeChargedOperatorCost`(additive)。**`84b3be0` 修复了 `3ea2285` 引入的配置回归(恢复已通过的 e2e 测试)**。
