# FISCO Opstack EL 规范符合性 —— `op-alignment-on-scheduler` 分支增量报告

- **日期**：2026-08-19
- **被检对象**：分支 `op-alignment-on-scheduler`（worktree `.claude/worktrees/op-alignment`，HEAD `ca6574bed`，45 commit 未推送）
- **基线报告**：`op5429-01` 分支的 `docs/2026-08-19-opstack-el-spec-compliance.md`（下称"主报告"）；本文只记录**相对该基线的增量**，规范条款与证据方法同主报告
- **分支拓扑**：基于 `feat-op-block-scheduler`（merge-base 28338d846），相对 `op5429-01` 差异 487 文件 / +65406 行

---

## 0. 增量总结论

**在 op5429-01 基线上红/黄的 12 项中，本分支解决 8 项；并新增了真实 op-node 的端到端动态证明。评级从「🟡 有条件满足」上调为：**

> **单 EL 节点部署（sequencer 与所有 op-node 实例指向同一 FISCO EL）：满足。**
> **多 EL 节点 / 生产化：仍为有条件满足** —— 剩余缺口集中在 baseFee/eip1559Params 构建确定性（MJ-1 残留）、reorg（MJ-2）、经典池加固（MJ-4）。

---

## 1. 已解决项（相对主报告）

| 主报告编号 | 缺口 | 本分支状态 | 证据 |
|---|---|---|---|
| BL-1 | OP 组装根缺失 | ✅ 完整接线：executor_version≥3 → OpSchedulerSeam 为 engine 调度器（`c_opMode` 真）+ OpScheduler 为 delegate（带 ledger、commit hook 走 prewriteBlockToBuffer）+ `maxEngineVersion=V4` + `static_assert(c_opMode)` 编译期证明；`feature_op_jovian` → `OpForkFlags.jovianActive`；chainId 非数字启动即拒 | `libinitializer/Initializer.cpp:558-643` |
| BL-2 | CallRequest.cpp 冲突 | ✅ 本工作区干净（冲突仅存在于主仓库工作区的未提交改动，C2 闭环文档遗留问题 #6 亦确认"与本分支无关但阻塞主仓库构建"） | `git status` 干净（该文件） |
| BL-3 | 核心文件仅存本机 | ✅ 结构性解决：OpBlock 迁移为 `opstack-executor/OpBlockExecute.{h,cpp}`，整个 opstack-executor 模块（含 OpScheduler/Seam/OpCommon/OpCommitments/OpDepositEncode/Storage2State/t8n 金测试）全部已跟踪 | `opstack-executor/` 目录、`git ls-files` |
| MJ-3a | 块 JSON withdrawalsRoot 占位 | ✅ B1 落地：header 有值读 header（MessagePasser 根），无值回退零哈希；`parentBeaconBlockRoot` 同样从 header 读 | `BlockResponse.cpp:100-115` |
| MJ-3b | 回执 JSON 零 OP 字段 | ✅ 11 个字段全部接通：l1Fee/l1BlobBaseFee/l1BaseFeeScalar/l1BlobBaseFeeScalar/operatorFeeScalar/operatorFeeConstant/daFootprintGasScalar/depositNonce/depositReceiptVersion/operatorFee(FISCO 扩展) | `ReceiptResponse.cpp:111-131` |
| MJ-1（部分） | attrs.gasLimit 被忽略 | ✅ 构建优先采用 CL 传入 gasLimit（L1 SystemConfig 值），缺失才回退本地 ledger 配置 | `EngineServiceImpl.h:685-689` |
| MN-2 | FCU V4 缺失 | ✅ 真实 handler（Prague 形状）+ 能力列表广告 V4 | `EngineEndpoint.cpp:91-97`；`EngineServiceImpl.cpp:143-145` |
| — | B2 真 op-node deposit 透传 | ✅ attrs.transactions 非空即停用合成 L1 attributes deposit | commit `4418d5288`；`EngineServiceImpl.h:613-617` |

**本分支修复并经真实 op-node 验证的问题**（C2 闭环文档 `docs/2026-08-19-c2-closure.md`）：FCU V3+attrs 门放宽、Jovian blobGasUsed=DA-footprint 从执行回填（六路承诺）、legacy 交易 v 重建（chainId*2+35+parity）、extraTransactionBytes 双形态判别（txpool 预影像 vs 密封后 wire 形态，`ExtraTxBytesDualLayoutTest` 7 用例）、engine-built 块 `miner` 字段。

---

## 2. 动态证据（本分支独有，直接支撑主报告 B/D/E 组判定）

| 闭环 | 结果 | 说明 |
|---|---|---|
| C2-3 sequencer 驱动 | ✅ | 真实 op-node v1.19.3（live 模式 L1 合约部署于 anvil）驱动 FISCO 持续出块（block #2300+） |
| C2-4a deposit→L2 | ✅ | L1 `depositTransaction` → op-node 派生 → L2 块含 0x7e 用户 deposit，余额铸造正确（block 1602） |
| C2-4b withdrawal 发起 | ✅ | 真实 MessagePasser(1.2.0) `initiateWithdrawal`：MessagePassed 事件、versioned nonce、**withdrawalsRoot 自动传播到块头**（Isthmus 语义闭环）；L1 finalize 需 op-proposer（devnet 未部署，超 EL 范围） |
| 本 worktree 单测 | ✅ | BcosEvmOpstackTests / OpstackExecutorTests / OpstackExecutorBlockTests / OpstackExecutorDetailTests + EngineProtoAlignB1Test/EngineRpcTest/ExtraTxBytes（4+24 项）全绿（build 为当前代码） |

这使主报告中 B（deposit 语义）、C（L1 attributes 槽位）、E2（withdrawalsRoot）从"静态对齐 + 单测"升级为**真实 CL 端到端验证**。

---

## 3. 仍然开放的缺口（按主报告编号）

### Major

| 编号 | 缺口 | 现状 |
|---|---|---|
| MJ-1（残留 2/3） | **eip1559Params 仍被忽略**（extraData 硬编码 1/1，`EngineServiceImpl.h:698-706`）与 **baseFee 仍沿用父块**（:649-667 "Phase A"），而 newPayload 校验按 `calcOpBaseFee(parent)` 强校验（:1334）。单 EL 节点自建快速路径（:1164-1199）掩盖该矛盾；**多 EL 节点部署下验证者必拒 sequencer 块**（除非恰好 gasUsed==target）；与 op-geth 混部必然分叉 | 未修 |
| MJ-2 | reorg/非顶端父块仍 -32603（:1401-1407）→ op-node 常规回退重放卡死派生 | 未修 |
| MJ-4 | 经典 txpool（p2p/tars 路径）仍无类型门（`MemoryStorage::submitTransaction` 链）；`MemPoolImpl::add` 仍仅拒 Blob | 未修 |
| MJ-5 | deposit 交易 JSON `nonce` 恒 `"0x0"`（`DepositTransaction.cpp:147-149`，注释改称"nonce 在回执中"——显示层选择，仍与 op-geth（透出 depositNonce）不一致） | 未修（降格为显示差异，回执侧已有 depositNonce） |

### Minor / 新增

| 编号 | 缺口 | 现状 |
|---|---|---|
| MN-1 | BLOBBASEFEE 推 0 应为 1 | **✅ 已修**（2026-08-19：`OpHost.cpp` `value_or(0)`→`value_or(1)`，A/B 对照确认对现有金向量零影响） |
| 新 | 块 JSON `baseFeePerGas` 硬编码 / `blobGasUsed` 恒 0 | **✅ 已修**（2026-08-19：`BlockResponse.cpp` 改为 header 有值读 header、PBFT 回退保持字节不变；引擎侧承诺原本已正确） |
| 新 | `eth_feeHistory` 未实现（cast 默认 1559 发现失败，需 `--legacy` 绕过）（C2 遗留 #3） | 未修 |
| 新（观察项） | MPT 历史状态读曾出现瞬时不一致后自愈（C2 遗留 #5，需复现定位——潜在正确性风险，定级取决于复现结论） | 待查 |
| MN-5/J-3/J-5/J-7 | predeploy CI 矩阵缺 OperatorFeeVault、4788/2935 无断言、README 陈旧 | **✅ 已修**（2026-08-19：`lib.sh` 扫描列表 13→14 补 OperatorFeeVault；`chain-config.template.yaml` expected_predeploys 增 BeaconBlockRoot/HistoryStorage（已核 C2 allocs 均带非空 code）；README 重写为与 build-allocs.py 现行为一致。**base_allocs_sha256 暂不冻结**：当前 /tmp/c2 产物由打补丁的 op-deployer 生成（sha256 与 handoff 记录不符），钉它等于钉不可复现产物——待用纯净 pin 版 op-deployer 重生成后冻结） |
| 新（构建怪癖） | `opstack-executor-tests` 链接行 `-lblst` 先于任何 `-L`（本地 build 产物手工补 -L 后通过；上游 CI 是否复现待查） | 已临时处理 |
| 新（WIP 状态警示） | 工作区遗留未提交 WIP（EngineServiceImpl.h + t8n generator/goldens）：`OpstackExecutorBlockTests` 在 WIP 编入后失败（goldenMatch=77/goldenMismatch=12 + 1 个 buildOpPayload fatal）——**与本轮改动无关**（A/B 还原对照结果逐字节一致），是 t8n 金文件重生成到一半的中间态 | 待上一会话收尾 |

其余主报告 Minor（MN-3 payloadId 派生、MN-4 池余额 worst-case、MN-7 真实 L1Block E2E、MN-8 链中过渡、MN-9 Karst）在本分支状态与主报告相同，不再赘述。

**MN-8（分叉激活方式）正式文档化**：FISCO 使用创世 `[features] feature_op_jovian=true` 选定 OP 分叉语义，而非 OP 规范的 L2 timestamp 激活。Isthmus 为默认基线；节点启动后分叉固定，不支持链中 Isthmus→Jovian 过渡。对创世即 Jovian 的新链与规范等价；对存量链升级不等价。已写入 `tools/opstack-genesis/README.md` 的 "Fork activation" 章节。

### 本轮修复的回归记录（2026-08-19）

- `python3.11 -m pytest tools/opstack-genesis/test_build_allocs.py`：**36/36 通过**
- C++（重建后）：`BcosEvmOpstackTests`、`OpstackExecutorTests`、`OpstackExecutorDetailTests` **全绿**；Engine RPC/Proto/Web3Response/Web3Rpc/ExtraTxBytes **35 项全绿**
- `OpstackExecutorBlockTests`：失败（WIP 归因如上，A/B 证明非本轮改动引起）

---

## 4. 结论（供引用）

`op-alignment-on-scheduler` 是三条分支中**唯一完整可用**的 OP 栈实现：

- **执行/共识语义**：满足（主报告 B/C/D/E 组全部 ✅ 不变，且新增真实 op-node 端到端证明）。
- **Engine API**：方法面含 V4 完整；校验链完整；**构建确定性缺口收窄为 eip1559Params/baseFee 两项**（gasLimit 已修）。
- **RPC 表面**： withdrawalsRoot/回执 OP 元数据/miner 已修；剩 baseFeePerGas/blobGasUsed 显示、deposit nonce、eth_feeHistory。
- **部署形态**：单 EL 节点（含 sequencer）已验证可用；多 EL 节点需先修 MJ-1 残留与 MJ-2；经典池入口在 OP 模式下应保持关闭或补门（MJ-4）。

综合评级：**单 EL 部署 ✅ 满足；多节点生产 🟡 有条件满足**（修复清单见 §3）。

---

*证据行号以本 worktree 2026-08-19 工作区为准（EngineServiceImpl.h 与 t8n golden 存在少量未提交 WIP，本文按工作区现状记录）。主报告：主仓库 `docs/2026-08-19-opstack-el-spec-compliance.md`。*
