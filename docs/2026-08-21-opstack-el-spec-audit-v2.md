# FISCO Opstack × OP Stack 执行客户端（EL）规范对齐审计 v2

- **日期**：2026-08-21
- **修订**：2026-08-22 按实施计划（docs/2026-08-22-opstack-engine-rpc-alignment-plan.md）修复 BL-3/MJ-1/MJ-2/MN-3/4/6/7/8，MN-2 改判 ✅；详见 §5 标注。
- **被检对象**：`feat-opstack-e2e` HEAD `b7d112b3f`（worktree `.claude/worktrees/op-alignment`；工作区未提交改动以文件当前内容取证）
- **规范基线**：specs.optimism.io（ethereum-optimism/specs@main，本地 `/tmp/op-specs`，commit `2049036`，2026-08-05）
- **分叉范围**：Isthmus（默认）+ Jovian（`feature_op_jovian`）+ **Karst 逐条审计**；Delta/Monsoon/Lagoon 概览扫描
- **参照实现**：op-geth `d0734fd5f44234cde3b0a7c4beb1256fc6feedef`（optimism 分支本地源码，逐条对照）；op-reth（main `7b3432d9` 已移除 OP 客户端；v1.2.0 `crates/optimism/payload` 用作 payloadId 参照；Karst 未合入任何公开 EL 客户端）
- **方法**：条款库驱动（6 个并行取证子任务 → 主线程汇总协调），条款总数 **237**（附录 A 完整条款库）
- **前作**：`docs/2026-08-19-opstack-el-spec-compliance.md`（v1，基线 op5429-01）——本报告逐项复核其缺口清单（§3）

图例：✅ 符合 / 🟡 部分符合 / ❌ 缺失 / ➖ 不适用（非 EL 责任，注明分工依据）

---

## 1. 总体结论

**核心执行语义（deposit/L1 attributes/费用/seal/回执根/预编译）已与规范及 op-geth 逐字节对齐（150+ 条款 ✅，附测试锚点）；剩余差距集中在四个方向：① Engine API 构建/错误面 5 处（其中 minBaseFee 构建侧 1 处 ❌）；② 派生边界 reorg 能力 2 处 ❌（生产 EL 硬门槛）；③ eth_getProof 历史块不可用 1 处 ❌（Isthmus 提款证明硬门槛）；④ Karst 整块未实现 8 处 ❌（与行业现状一致——op-geth 亦未实现 Karst 执行语义）。**

按「能否作为 op-node 的执行引擎后端」判据：

| 层 | 结论 | 说明 |
|---|---|---|
| 执行/共识语义（deposits、fees、seal、receipts、precompiles、l1-attributes） | **✅ 高度符合** | M1/M2 共 131 条款中 100 ✅；含 FastLZ 逐字节移植、三套费用公式、DA footprint、Isthmus withdrawalsRoot/requestsHash、P256 等预编译全表，均有测试锚点 |
| Engine API 方法面/校验面 | **✅ 大体符合** | S-EXE 33 条中 29 ✅：V1–V4 方法面、JWT、能力协商、extraData 形状、baseFee 重算校验、六向比较均到位；欠账：错误码映射（S-EXE-17 🟡）、FCU attrs 深度校验（S-EXE-5/32 🟡） |
| 区块构建面 | **🟡 基本符合，1 处 ❌** | gasLimit/eip1559Params/baseFee 重算均已消费（v1 的 MJ-1 三硬编码已关闭两项半）；**attrs.minBaseFee 未写入 Jovian extraData（S-BLD-4 ❌）**——跨客户端分叉风险 |
| 派生边界（reorg/consolidation） | **❌ 缺失 2 处** | S-DRV-6/7：非 tip parent 拒绝（-32603）、FCU head 回退静默忽略——L1 reorg 时节点无法恢复。生产化硬门槛 |
| 用户 RPC 面 | **🟡 1 处 ❌** | v1 的块/回执/deposit 字段缺口全部已修复（M6 复核）；**eth_getProof 非创世块不可用（S-RPC-6 ❌）**，Isthmus 提款证明无法生成 |
| Karst | **❌ 未实现** | 14 条中 0 ✅：NUT 机制/激活/预编译上限全缺；EVM 层 Osaka 原语已就绪但 OP 路径未绑定。op-geth/op-reth 同样未实现 Karst 执行语义 |
| 创世/predeploy/preinstall | **✅ 符合** | allocs 注入、15 preinstall 逐地址实证（CreateX keccak 命中规范值）、feature_flags 创世验证均到位 |

**综合评级：🟡 有条件满足（core-green, engine-api-amber, reorg/proof-red, karst-grey）**——完成 §5 的 BL-1~BL-4 后具备与 op-node 生产对接的条件；Karst 按基线裁定为后续工作线。

---

## 2. 判定汇总（237 条款）

| 子任务 | 模块 | 条款数 | ✅ | 🟡 | ❌ | ➖ |
|---|---|---|---|---|---|---|
| M1 | 执行语义（S-DEP/S-L1A/S-FEE） | 86 | 70* | 3 | 0* | 14 |
| M2 | withdrawals/precompiles/predeploys/preinstalls/system-config（S-WDL/S-PRE/S-PDE/S-PIN/S-SYC） | 45 | 30 | 2 | 0 | 13 |
| M3+M4 | Engine API + 构建（S-EXE/S-BLD） | 46 | 39 | 3 | 1 | 3 |
| M5 | 派生边界（S-DRV） | 19 | 6 | 0 | 2 | 11 |
| M6 | RPC + 创世（S-RPC/S-GEN） | 27 | 23 | 2 | 1 | 1 |
| M7 | Karst（S-KAR）+ 概览 | 14 | 0 | 5 | 8 | 1 |
| **合计** | | **237** | **168** | **15** | **12** | **43** |

\* M1 原始计数为 ✅66 / ❌3（S-FEE-29/32/33/34 执行库视角）；经 §4 跨层协调后 29/32/34 由引擎层实现覆盖（✅），33 与 S-BLD-4 合并为同一缺口。有效 ❌ 总数为 12（S-FEE-33 并入 S-BLD-4）。

---

## 3. 与 v1 报告（2026-08-19）缺口清单对照

### 已关闭

| v1 编号 | 缺口 | 关闭证据（本报告条款） |
|---|---|---|
| BL-1 | 分支汇聚（scheduler/校验函数/透传未合入，OpstackExecutor 零消费者） | 已汇聚：`OpScheduler.h/Seam`、`buildOpPayload`、`calcOpBaseFee`、`validateOpNewPayloadRequest` 均在 HEAD；姊妹分支仅剩 docs/style 差异（ahead=2/3） |
| BL-2 | CallRequest.cpp 冲突标记 | 已解（测试可编译运行，§7） |
| BL-3 | OpBlock 等仅存本机文件 | 已关闭：`opstack-executor/OpBlockExecute.{h,cpp}` 等已 tracked（`git ls-files` 确认） |
| MJ-1 | 构建侧 gasLimit/eip1559Params/baseFee 三硬编码 | 部分关闭：gasLimit 消费（S-BLD-2 ✅）、eip1559Params→extraData 编码（S-BLD-3 ✅）、baseFee 按 calcOpBaseFee 重算（S-BLD-5 ✅）；**剩余 minBaseFee（S-BLD-4 ❌）** |
| MJ-3 | 回执/块 JSON OP 字段缺失 | 已关闭：ReceiptResponse 13 字段全输出、BlockResponse 从 header 真值读取（S-RPC-8~11 ✅，M6 逐项复核） |
| MJ-4 | 经典 txpool 无类型门 | 已关闭：EthEndpoint 双门 + MemPoolImpl 门（S-RPC-5/13/14 ✅） |
| MJ-5 | deposit tx JSON nonce 恒 0x0 | 已关闭：mined deposit 用回执 depositNonce 覆盖（S-RPC-1 ✅） |
| MN-1 | BLOBBASEFEE 推 0 | 已修复：`OpHost.cpp:101` `value_or(1)`（S-FEE-35 ✅） |
| MN-2 | FCU V4 未实现 | 已实现：V4 三件套接线 + 能力通告（S-EXE-10/15 ✅） |
| MN-3 | payloadId 计数器实现 | 已规范化：`derivePayloadId` 与 op-geth `BuildPayloadArgs.Id()`、op-reth `payload_id_optimism` 字节对齐，golden 单测（S-DRV-5 ✅） |
| MN-6 | base_allocs_sha256 未冻结 | 已冻结：build-allocs.py:207-231 溯源钉扎 + CLI 必填（S-GEN-7 ✅） |
| MN-7 | 真实 L1Block 字节码 E2E 缺失 | 已补：OpL1BlockRealBytecodeTest 全量 SSTORE 槽位断言（S-L1A-05/09 ✅） |

### 未关闭（本报告重新定位）

| v1 编号 | 缺口 | v2 状态 |
|---|---|---|
| MJ-2 | reorg/非顶端父块 -32603 拒绝 | **未关闭** → S-DRV-6（❌）+ S-DRV-7（❌），§5 BL-1 |
| MN-4 | 池余额校验不含 L1/operator worst-case | 仍缺（S-FEE-20 🟡）——架构性：FISCO 无 txpool 组件，执行路径有等价 512-bit cap |
| MN-5 | predeploy 矩阵缺 OperatorFeeVault 等 | 已补足：OperatorFeeVault 地址/计费/注入实证（S-PDE-12 ✅）；15 preinstall 逐地址实测（S-PIN-1 ✅）；剩余：无逐地址断言脚本（依赖 sha256 钉扎） |
| MN-8 | 链中分叉过渡不支持 | 仍缺（架构性，S-SYC-6 注明）：feature flag 创世固定 vs 规范时间戳激活 |
| MN-9 | Karst 占位 | 已从"占位"升级为**逐条审计结论**：14 条中 0 ✅（§5 BL-4/MJ-3） |

---

## 4. 跨层判定协调说明（汇总时归并，避免双计）

M1 从"执行库"视角、M3/M5 从"引擎层"视角取证同一特性，以下 5 条经协调归并（全部有引擎层代码锚点，`engine/bcos-engine/EngineServiceImpl.{h,cpp}`）：

| 协调前（M1 判定） | 协调后 | 依据 |
|---|---|---|
| S-FEE-01（🟡 无 1559 参数公式） | ✅ 引擎层实现 | `calcOpBaseFee` 默认 8/2 并从父头 extraData 读（EngineServiceImpl.cpp:299-312；S-EXE-24） |
| S-FEE-29（❌ 无 gasMetered） | ✅ 引擎层实现 | `calcOpBaseFee` Jovian `max(gasUsed,blobGasUsed)`（:336-340；S-EXE-25），与 op-geth eip1559.go:98-106 逐行等价 |
| S-FEE-32（❌ 无 extraData 解码/校验） | ✅ 引擎层实现 | 形状/版本/非零校验（:496-537；S-EXE-21）+ 构建编码（EngineServiceImpl.h:725-753；S-BLD-3） |
| S-FEE-33（❌ minBaseFee 无实现） | 🟡 半实现 → **并入 S-BLD-4** | 读取/钳制侧已就绪（:316-328,370-374），构建侧恒零（EngineServiceImpl.h:747-751）→ 唯一真实缺口 S-BLD-4 |
| S-FEE-34（❌ 无 eip1559Params 解析） | ✅ 引擎层实现 | 8 字节强制解析（EngineHelper.cpp:338-357；S-EXE-8） |

另有 2 处跨 agent 存疑点记录（不改变判定，供后续确认）：① deposit 回执 L1/operator/DA 字段来源——op-geth 从 attributes calldata 解析、FISCO 从 L1Block 存储槽读取，正常路径一致，仅 attributes deposit 执行失败时出现分歧（低概率）；② Jovian 激活块判定——FISCO 与 op-geth 均用 176B 长度启发式，与规范"selector 区分"字面不同，两参照一致。

---

## 5. 差距清单（按严重度）

### Blocker（生产 EL / op-node 对接硬门槛）

| # | 缺口 | 规范条款 | FISCO 证据 | 参照实现 | 修复指向 |
|---|---|---|---|---|---|
| BL-1 | **reorg/consolidation 能力缺失（S-DRV-6/7 ❌）**：非 tip parent 的替代 payload 被 -32603 拒绝（Step 3c 自认 parked）；FCU head 回退被静默忽略 | derivation.md:824-827（"enables execution engines like go-ethereum to enact the change"）、:898-914 | EngineServiceImpl.h:1432-1468、:341-349 | op-geth `InsertBlockWithoutSetHead` + FCU `SetCanonical` 回退（eth/catalyst/api.go:278-293,760-767） | blockHash→block 映射 + 非 canonical 头导入 + FCU 回退更新；至少支持 tip 回退一层的替代属性处理 |
| BL-2 | **eth_getProof 非创世块不可用（S-RPC-6 ❌）**：MPT 节点仅 genesis 导入写盘，运行时块不持久化 → 任意历史块提款证明（withdrawalProof）无法生成 | isthmus/exec-engine.md:58-59（"The storage root should be the same root that is returned by eth_getProof"） | Ledger.cpp:2398；tools/op-e2e/rpc_matrix.py:221-224（自认 -32004/-32602 边界） | op-geth GetProof 对任意块 header.Root 开 trie（internal/ethapi/api.go:382-471） | 运行时 MPT 节点持久化（或按需重建路径） |
| BL-3 | **构建侧 minBaseFee 未写入 Jovian extraData（S-BLD-4 ✅，含 S-FEE-33/S-SYC-10/12）**：extraData 尾 8 字节恒零；attrs.minBaseFee 解析后无消费；缺 post-Jovian 必填校验 —— **✅ 已修复（2026-08-22，commit d498342f5 + 5ee1666ad/5544050d1，实施计划 Task 3+2）** | jovian/exec-engine.md:40-54,79；jovian/system-config.md:23-68 | EngineServiceImpl.h:747-751（`extra.resize(17,0x00)`）；EngineHelper.cpp:358-369（仅解析） | op-geth `EncodeJovianExtraData` 写 u64 BE（eip1559_optimism.go:49-54,180-190）；缺失报 "missing minBaseFee"（miner/worker.go:380-394） | buildOpPayload 消费 attrs.minBaseFee → extraData[9,17)；FCU attrs 校验补必填/形状 |
| BL-4 | **Karst 激活即链断（S-KAR-9~14 ❌）**：无 NUT 机制（31 笔升级交易 bundle 未嵌入、无注入/顺序/gas 分配）、无 timestamp/feature 激活、无 Karst 预编译上限（bn256Pairing 57,600B，S-KAR-1） | karst/derivation.md:13-34；karst/exec-engine.md:16-25；l2-upgrades-1-execution.md | OpForkSchedule.cpp:88-101（jovianConfig 别名）、configAt 永不返回 Karst；OpPrecompiles.cpp:35-41（81984） | **行业现状：op-geth 仅 KarstTime 谓词无执行门控；op-reth main 已移除 OP 客户端、v1.9.4 最远 Jovian**；op-node develop 已含 KarstNUTBundleJSON（bundles.go:7-10） | 独立工作线：Karst→EVMC_OSAKA 绑定（EVM 原语已就绪）+ NUT 注入/激活/gas 分配 + bn256 上限 |

### Major（多节点/对接 op-node 前应修）

| # | 缺口 | 规范条款 | FISCO 证据 | 参照实现 | 修复指向 |
|---|---|---|---|---|---|
| MJ-1 | **engine 错误码映射未接线（S-EXE-17 ✅）**：UnsupportedFork(-38005)/UnknownPayload(-38001)/InvalidForkchoiceState(-38002) 一律转 -32603；EngineEndpoint.cpp:158 的 -38001 分支为死代码 —— **✅ 已修复（2026-08-22，commit d21bad656 + 88f33ce34，实施计划 Task 1）** | exec-engine.md:206（execution-apis 错误码语义） | Web3JsonRpcImpl.cpp:91-103（`catch (bcos::Error)` → -32603）；EngineServiceImpl.h:78-84 自述 not implemented | op-geth 类型映射（beacon/engine/types.go:161-180） | 错误类型→JSON-RPC 码映射表 |
| MJ-2 | **FCU 带 attrs 深度校验缺失（S-EXE-5/32 ✅）**：缺 gasLimit 回退 ledgerConfig 而非 INVALID；eip1559Params 尺寸非法、withdrawals 非空、缺 parentBeaconBlockRoot 被静默规整而非 -38003 —— **✅ 已修复（2026-08-22，commit 5ee1666ad + 5544050d1，实施计划 Task 2）** | exec-engine.md:265-267（"required when used as rollup"） | EngineServiceImpl.h:270-283（`if constexpr (!c_opMode)` 跳过预检）、:714-716、:703-704、:757 | op-geth `checkOptimismPayloadAttributes` FCU 即拒（api_optimism.go:40-65 → STATUS_INVALID + -38003） | OP 面启用 validatePayloadAttributes + gasLimit 必填。线级形态为 200+STATUS_INVALID 而非 -38003 错误（op-node 对两形态等价处理，2026-08-22 最终审查确认）|
| MJ-3 | **Karst 逐条欠账（S-KAR-2/3/4/6/7 🟡）**：Osaka EIP 原语（7823 MODEXP 上限、7825 gas cap、7883 MODEXP 费、7939 CLZ、7951 P256 gas 6900）在 EVM 层就绪但 OP 路径 rev=PRAGUE 不生效；P256 被 3450 override 钉死 | karst/overview.md:19-26 | OpForkSchedule.cpp:97（karstConfig rev=EVMC_PRAGUE）；OpPrecompiles.cpp:29,37（3450 override） | op-geth EIP 原语（contracts.go:712,738；eips.go:299-300；protocol_params.go:42） | Karst→EVMC_OSAKA 绑定 + 取消 P256 override |
| MJ-4 | **S-KAR-5 ❌：eth_config RPC 缺失**（EIP-7910，SHOULD 接口面） | karst/overview.md:23 | bcos-rpc 无 Config 方法 | op-geth internal/ethapi/api.go:1408 | 新增 RPC 方法 |

### Minor（不阻塞对接，择期）

| # | 缺口 | 说明 |
|---|---|---|
| MN-1 | S-FEE-20（🟡）：txpool worst-case 余额拒绝无实现 | 架构性——FISCO 无 txpool 组件；执行路径 512-bit cap 兜底（OpTransition.cpp:412-421） |
| MN-2 | S-FEE-28（✅）：无显式 `daFootprint <= gasLimit` 校验 —— **改判 ✅（2026-08-22 四维审查）**：op-geth core/block_validator.go:131-134 有 daFootprint>GasLimit 检查，FISCO EngineServiceImpl.cpp:553-557 有等价检查 | 与 op-geth 行为一致（构建侧约束）；校验端仅 blobGasUsed≤gasLimit（EngineServiceImpl.cpp:557） |
| MN-3 | S-RPC-12（✅）：block JSON 不输出 requestsHash —— **✅ 已修复（2026-08-22，commit 01b7c9043，实施计划 Task 4）** | 共识值正确（sha256('')）；仅 RPC 输出与 op-geth 不一致 |
| MN-4 | S-GEN-3（✅）：Isthmus 创世 withdrawalsRoot 工具链默认空根 —— **✅ 已修复（2026-08-22，commit d4983745b + a6c6c7bd6，实施计划 Task 8）**：C2 base allocs 的 passer 实为 EIP-1967 代理布局（2 槽），修复前空根对本 artifact 是错误的；工具现在计算实际存储根（0x8ed4baae…），rpc_matrix.py 创世断言已改为 header==getProof 不变量 | Phase A MessagePasser 直接部署空存储；root=实际存储根自洽；切 op-deployer 代理布局须重生成 artifact |
| MN-5 | FCU V3 建块 vs newPayload V4-only 提交不对称 | 若 op-node 协商全链 V3，自建块回送被 UnsupportedFork 拒绝；需 op-node 版本选择验证 |
| MN-6 | deposit tx JSON 缺 depositReceiptVersion —— **✅ 已修复（2026-08-22，commit d215b7f53，实施计划 Task 5）** | op-geth tx 响应也输出（api.go:1210-1213）；FISCO 仅回执输出 |
| MN-7 | 过期注释：EngineServiceImpl.h:1140-1152（"#5429 finding B" 称 V4 不可达） —— **✅ 已修复（2026-08-22，commit d746472c2，实施计划 Task 6）** | 与当前代码矛盾（V4 已接线），误导维护 |
| MN-8 | chain-config.yaml 模板注释"feature_flags 由 C++ 注入"与 Ledger.cpp 实际"验证"行为不符 —— **✅ 已修复（2026-08-22，commit 44b2c5ffb，实施计划 Task 7）** | 纯文档问题 |
| MN-9 | S-SYC-6 架构差异：feature flag 创世固定 vs 规范运行时 toggle/时间戳激活 | 新链语义等价；存量链升级不等价，文档已注明 |

---

## 6. Karst 专项结论 + Delta/Monsoon/Lagoon 概览

### Karst（14 条：✅0 / 🟡5 / ❌8 / ➖1）

- **本质结论**：`karstConfig()` 为 jovianConfig 别名（OpForkSchedule.cpp:88-101）+ `configAt()` 永不返回 Karst（:103-110）+ 无 feature_op_karst——三重占位，Karst 当前不可达且无任何专属语义。**spec 的 Karst 有大量独立执行语义**（bn256Pairing 上限 57,600B、Osaka EIP 包、NUT 31 笔升级交易），与"Jovian 别名"存在本质差距。
- **行业现状（重要背景）**：Karst 未合入任何公开 EL 客户端——op-geth 仅有 KarstTime 谓词无执行门控；op-reth main（7b3432d9）已整体移除 OP 客户端、v1.9.4 最远到 Jovian。op-node develop 已含 KarstNUTBundleJSON 与 NUT 通用机制（upgrade_transaction.go:90,125-127）。
- **EVM 层已就绪**：evmone 定制版已具备 EVMC_OSAKA 与全部 Osaka EIP 原语（EIP-7823/7825/7883/7939/7951 的以太坊值，precompiles.cpp:42,127,155-166,179、state.cpp:387、instructions_traits.hpp:177,258），差距集中在 **OP fork 绑定（rev=EVMC_OSAKA）与 NUT 机制**。

### 概览扫描

- **Delta**：纯 CL（span-batches batch 格式 v1/v2），无 EL 执行语义影响。
- **Monsoon / Orogeny / Permafrost / Narrows**：空壳章节，无 EL 要求。
- **Lagoon（EL 侧最大新增面，需 EL 实现、逐条另行裁定）**：0x7D post-exec 交易（新 EIP-2718 类型，最后位置/无签名/收据继承/DA footprint 排除）+ SDM v1 结算（canonicalGasUsed、四向 settle、opGasRefund RPC 字段）——FISCO 全缺（classifyTxType 无 0x7D）。
- **custom-gas-token**：新 predeploy 集（NativeAssetLiquidity/L1BlockCGT 等），与 Karst NUT bundle 部署对象重叠。
- **l2-upgrades-1-execution**：NUT 机制与激活块 gas 分配（Σ bundle gasLimit 提升、次块恢复）——已被 S-KAR-9~14 覆盖。
- 其余（pectra-blob-schedule、guaranteed-gas-market、revshare、safe-extensions、stage-1、superchain-upgrades、proposals、flashblocks）：CL 或 L1 合约职责，无 EL 执行要求。

---

## 7. 动态验证记录

| 项 | 结果 |
|---|---|
| op-geth 参照 commit | `d0734fd5f44234cde3b0a7c4beb1256fc6feedef`（optimism 分支） |
| BcosEvmOpstackTests（build/bcos-evm/test/bcos-evm-opstack-tests） | **115/115 PASS**（0.08s，Aug 20 构建） |
| OpstackExecutorTests（build/opstack-executor/tests/opstack-executor-tests） | **23/23 PASS**（0.07s，Aug 21 构建，与工作区同步） |
| OpstackExecutorDetailTests | **19/19 PASS**（0.03s，Aug 19 构建） |
| OpstackExecutorBlockTests | 1 FAIL（`ForkchoiceAttributesVersionGate`，OpNewPayloadRpcE2eTest.cpp:1319）——**陈旧二进制**（Aug 19 构建，早于 8/21 buildOpPayload 重构批次；同源用例在最新二进制 23/23 中通过），未触发重编译验证（>10min 放弃） |
| op-reth 参照 | main `7b3432d9`（已移除 crates/optimism）；raw 拉取 v1.2.0 `crates/optimism/payload` 6 文件（1654 行）作 payloadId 参照：`payload_id_optimism`（payload.rs:308-355）与 FISCO `derivePayloadId` 同构 |
| 证据纪律 | 各子任务全部行号经 grep/Read 验证；❌/🟡 条款 100% 给出参照实现位置；未修改 worktree 任何文件 |
| 2026-08-22 回归（实施计划 Task 9） | BcosEvmOpstackTests 115/115、OpstackExecutorTests 23/23、OpForkchoiceRpcE2eSuite 全绿、test-bcos-rpc 四 suite 全绿、EngineServiceTest 全绿、test_gen_eth_header_fixture 2/2；block-tests 全量 8 个 OpL1BlockDepositSuite 失败为 HEAD 既有（stash 实验证实与任务无关，另查） |

---

## 8. 存疑点（不改变判定，建议后续确认）

1. **deposit 回执 L1/operator/DA 字段来源**：op-geth 从 attributes calldata 解析（rollup_cost.go:410-454），FISCO 从 L1Block 存储槽读取（OpFeeParams.cpp:36-46）——正常路径一致（attributes deposit 将 calldata 写入槽），仅 attributes deposit 执行失败时分歧（低概率；op-node 保证 attributes deposit 不失败）。
2. **Jovian 激活块判定**：FISCO 与 op-geth 均用 176B 长度启发式（OpBlockExecute.cpp:36-44 vs rollup_cost.go:571-577），规范字面要求 selector 区分——两参照实现一致采用长度启发式，建议确认规范意图。
3. **S-DEP-05 内容检查降级**：首笔 deposit 非 L1-attributes 仅 WARNING（OpBlockExecute.h:240-244），与 op-geth/op-reth 验证端一致，但严于规范 MUST 字面；恢复硬拒绝会造成与参照验证端分叉。
4. **回执哈希域**：depositNonce/version 的 receiptsRoot 编码为 FISCO tars 哈希域而非 EIP-2718 RLP，跨客户端 receiptsRoot 一致性依赖 M5 回执编码（OpBlockExecute.cpp:244-276 全字段 EIP-658 编码，共识侧已有对拍测试）。
5. **FCU V3/V4 不对称**：见 MN-5，需 op-node 实际版本选择验证（当前 C2 链打通记录为 FCU V3 建块 + newPayloadV4 提交）。

---

## 附录

- **附录 A**：完整条款库（237 条，按模块，含每条规范要点/来源/MUST 级别/判定/双轨证据）——见下文各模块表。
- **附录 B**：参照版本与拉取记录——op-geth `d0734fd5f`；op-reth main `7b3432d9`（无 OP 客户端）、v1.2.0 optimism-payload（raw）；op-node develop（raw，Karst NUT 参照）；spec `2049036`。

---

# 附录 A：完整条款库（237 条）


## 模块：m1-exec-semantics

# M1 审计：执行语义（deposits / L1 attributes / fees）

- **模块**：OP Stack 执行客户端（EL）spec 对齐审计 — M1（执行语义）
- **负责章节**：
  - `/tmp/op-specs/specs/protocol/deposits.md`（全文）
  - `/tmp/op-specs/specs/protocol/ecotone/l1-attributes.md`、`isthmus/l1-attributes.md`、`jovian/l1-attributes.md`
  - 费用条款：`fjord/exec-engine.md`（FastLZ 估算）、`isthmus/exec-engine.md`（operator fee）、`jovian/exec-engine.md`（×100、DA footprint）、`holocene/exec-engine.md`（动态 1559 费用部分）、`protocol/exec-engine.md`（1559 参数 / Fee Vaults / L1-Cost fees / Ecotone disable Blob / Deposited transaction processing）、`regolith/overview.md`、`canyon/overview.md` 相关 diff
- **分叉基线**：Isthmus 默认 + Jovian（feature_op_jovian）；Karst 不归本模块
- **规范 commit**：2049036afe878a7cb443f513f4e6ca453d90c340（/tmp/op-specs）
- **op-geth commit**：d0734fd5f44234cde3b0a7c4beb1256fc6feedef（/Users/octopus/octo/code/op-geth，optimism 分支本地源码）
- **FISCO 路径前缀**：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment/`（下文简写为 FISCO 根）

**判定标准**：✅ 符合（行为一致，附可 grep 锚点与测试证据）；🟡 部分符合（占位/硬编码/架构映射不等价，写明偏差与影响）；❌ 缺失（无实现/显式拒绝/未接线，给规范依据+参照实现位置）；➖ 不适用（非 EL 责任，注明分工依据）。

**重要架构说明**（影响多处判定）：FISCO 的最小 OP 验证器循环只支持 Isthmus+（`OpForkSchedule.h:11-14` "FB only MODELS Ecotone+"，引擎 -38005 门拒 pre-Isthmus payload，`OpForkSchedule.cpp:103-110` configAt 只返回 Isthmus/Jovian）。Regolith/Canyon 语义是 baseline，Bedrock 费用公式不可达。L1 attributes deposit 由外部（op-node/sequencer）构造，EL 侧只解码+执行+消费其写入的 L1Block 存储。

---

## S-DEP：deposits.md（含 exec-engine.md Deposited transaction processing）

| 编号 | 规范要点（原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-DEP-01 | "We define a new EIP-2718 compatible transaction type with the prefix 0x7E"（deposit 交易类型前缀 0x7E） | deposits.md:62 | MUST | Bedrock+ | ✅ | `kDepositTxType = 0x7e` OpTransition.h:171；信封编码前缀 OpDepositEncode.h:19；解码校验类型字节 OpstackExecutor.h:307 | DepositTxType=0x7E core/types/deposit_tx.go:27 | |
| S-DEP-02 | 字段：sourceHash/from/to(可空=创建)/mint/value/gas/isSystemTx/data（RLP 顺序） | deposits.md:69-78 | MUST | Bedrock+ | ✅ | `DepositTx` 结构 OpTransition.h:157-168；严格解码（宽度/规范 RLP）OpstackExecutor.h:252-402 | DepositTx struct core/types/deposit_tx.go:29-46 | 测试：OpDepositTxTest.cpp:10/28（null to、mint nullopt vs 0） |
| S-DEP-03 | "Does not include a nonce, since it is identified by the sourceHash"（无 nonce 字段，sourceHash 标识） | deposits.md:82-85 | MUST | Bedrock+ | ✅ | DepositTx 无 nonce 字段（OpTransition.h:157-168）；信封解码亦无 nonce 项 | nonce()=0 deposit_tx.go:79 | |
| S-DEP-04 | sourceHash 派生公式（用户 0/属性 1/升级 2 域 + keccak 嵌套） | deposits.md:100-113 | MUST | Bedrock+ | ➖ | 派生属 op-node derivation（op-node 生成信封）；EL 侧仅解码并原样携带（OpstackExecutor.h:323 source_hash 拷贝） | 仅携带字段 deposit_tx.go:30 | 分工：L2 链派生过程（rollup node）计算 sourceHash；EL 不校验其内容 |
| S-DEP-05 | "The first transaction MUST be a L1 attributes deposited transaction"（块首笔必须是 L1 attributes deposit） | deposits.md:131 | MUST | Bedrock+ | ✅ | 块级强制：processOpBlock.cpp:78-83（首笔非 deposit 抛块级错误）；preBlockOpSteps 同样强制类型字节 0x7e（OpBlockExecute.h:228-234）。内容级检查（to/from）降级为 WARNING（OpBlockExecute.h:240-244），与 op-geth 验证端一致 | op-geth 验证端不检查首笔内容（FISCO 注释 OpBlockExecute.h:236-239 注明 op-geth/op-reth 均接受非 L1-attributes 首笔 deposit） | 测试：OpSchedulerTest/OpBlockInjectorTest 均为 deposit-first 块 |
| S-DEP-06 | user-deposited 仅出现在 epoch 首块 | deposits.md:134 | MUST | Bedrock+ | ➖ | 属 derivation（op-node）产出约束；EL 验证端不校验。FISCO 对 deposit-after-non-deposit 仅记 WARNING 不拒绝（OpstackExecutor.h:532-541），与 op-geth/op-reth 验证端一致 | 同左（FISCO 注释明示） | |
| S-DEP-07 | "the balance of the from account MUST be increased by the amount of mint. This is unconditional, and does not revert on deposit failure"（mint 无条件增加） | deposits.md:150-151 | MUST | Bedrock+ | ✅ | runDeposit mint 直接加余额 OpTransition.cpp:500-501；两条失败路径均保留 mint（:533-536, :544-547） | execute() mint 先行 state_transition.go:469-475；失败分支回滚到 mint 后 :481-503 | 测试：SuccessMintsAndAdvancesNonce OpDepositTest.cpp:58-86；EvmRevertKeepsMintAndChargesActualGas :88-117 |
| S-DEP-08 | 执行环境按与 EIP-155 交易完全相同方式初始化 | deposits.md:153-154 | MUST | Bedrock+ | ✅ | 共享执行核 runTxMessage（OpTransition.cpp:179-226，warm 访问/refund/7623 floor 一致） | innerExecute state_transition.go:507-717 | |
| S-DEP-09 | 无 fee 字段验证（deposit 无 fee 字段） | deposits.md:158 | MUST | Bedrock+ | ✅ | max_gas_price=max_priority=0（OpTransition.cpp:511-512），无 gas price 校验；validateBlock.base_fee=0（:517-518） | preCheck deposit 分支跳过全部 fee 检查 state_transition.go:342-356 | |
| S-DEP-10 | 无 nonce 字段验证 | deposits.md:159 | MUST | Bedrock+ | ✅ | nonce 直接取 state 现值（preNonce, OpTransition.cpp:499,513），无校验比较 | 同上 :342-356 | |
| S-DEP-11 | 无 access-list 处理（视为空） | deposits.md:160 | MUST | Bedrock+ | ✅ | DepositTx 无 access_list 字段；tx 构造不填（OpTransition.cpp:503-513） | accessList()=nil deposit_tx.go:72 | |
| S-DEP-12 | 不检查 from 是否为 EOA | deposits.md:161-162 | MUST | Bedrock+ | ✅ | DepositValidationView 屏蔽 EIP-3607（code_hash 置空, OpTransition.cpp:466-468, 519） | preCheck 对 deposit 跳过 EOA 检查 state_transition.go:342-344 | 测试：SenderWithCodeIsAllowed OpDepositTest.cpp:553-577 |
| S-DEP-13 | 无 ETH gas 退款 | deposits.md:167 | MUST | Bedrock+ | ✅ | gas_price=0 → 退款恒 0（OpTransition.cpp:511；RefundLowersDepositGasUsed 测试证实 gas_used 降但余额不变） | "no ETH refunded … gasPrice is always 0" state_transition.go:652-654 | 测试：RefundLowersDepositGasUsed OpDepositTest.cpp:202-230 |
| S-DEP-14 | 无 priority fee、无 coinbase 支付 | deposits.md:168 | MUST | Bedrock+ | ✅ | runDeposit 不 touch coinbase/SequencerFeeVault；失败/成功均不支付 | 对 deposit 跳过 coinbase 支付（Regolith）state_transition.go:655-663 | 测试：SuccessMintsAndAdvancesNonce 断言无 L1_FEE_VAULT 触碰（OpDepositTest.cpp:85） |
| S-DEP-15 | 无 L1-cost fee（deposit 不交 DA 数据费） | deposits.md:169 | MUST | Bedrock+ | ✅ | runDeposit 完全不调用 computeL1Cost | L1CostFunc 对空 RollupCostData 返回 nil（rollup_cost.go:196-197） | |
| S-DEP-16 | 无 base fee，总 base fee 账目不改变 | deposits.md:170 | MUST | Bedrock+ | ✅ | validateBlock.base_fee=0（OpTransition.cpp:517-518）；runDeposit 不 touch OP_BASE_FEE_VAULT | 同 S-DEP-14 | |
| S-DEP-17 | 合约创建行为与 gas metering 相同（含 intrinsic gas） | deposits.md:172-174 | MUST | Bedrock+ | ✅ | runTxMessage 共享 EVM 路径；validate_transaction 计算 intrinsic（OpTransition.cpp:520-521）；CREATE 地址用执行前 nonce（toFiscoContractAddress OpTransition.cpp:151-157,573） | IntrinsicGas state_transition.go:72-118；Create/Call :588-613 | 测试：ContractCreationDerivesAddressFromPreExecutionNonce OpDepositTest.cpp:144-176 |
| S-DEP-18 | 非 EVM 状态转移错误 → EVM 错误（deposit 总是被包含，receipt 指示失败） | deposits.md:176-181 | MUST | Bedrock+ | ✅ | 失败分支 receipt.status=EVMC_FAILURE、gas_used=gas_limit（OpTransition.cpp:527-536, 540-547） | deposit 失败分支 state_transition.go:481-503 | 测试：EntryFailureChargesFullGasLimitButKeepsMint OpDepositTest.cpp:119-142；ValueOverPostMintBalanceFailsWithFullGasLimit :578-604 |
| S-DEP-19 | 世界状态回滚到 mint 之后 | deposits.md:182 | MUST | Bedrock+ | ✅ | State 自 view 拷贝（OpTransition.cpp:497），失败路径不应用 EVM diff（仅 mint+nonce 入 diff）；diff 经 sanitizeStateDiff | RevertToSnapshot(snap) state_transition.go:486 | 测试：EvmRevertKeepsMintAndChargesActualGas OpDepositTest.cpp:88-117 |
| S-DEP-20 | from 账户 nonce +1（错误后强制递增） | deposits.md:183-184 | MUST | Bedrock+ | ✅ | 失败分支显式 nonce=preNonce+1（OpTransition.cpp:533,544）；成功路径 ++（:553-554） | SetNonce(+1) state_transition.go:488 | |
| S-DEP-21 | 执行后 gas pool 与 receipt 处理同常规交易 | deposits.md:186-187 | MUST | Bedrock+ | ✅ | blockGasLeft 扣除（OpstackExecutor.h:626；processOpBlock.cpp:105-107）；gas_limit 超块预算为块级错误（OpTransition.cpp:529-530） | SubGas/gas pool state_transition.go:355；ErrGasLimitReached :481 | 测试：GasLimitOverBlockBudgetIsBlockError OpDepositTest.cpp:605-625；GasLimitExactlyBlockBudgetIsAccepted :626-648 |
| S-DEP-22 | Regolith 起 receipt 增加 depositNonce = EVM 处理前 from 的 nonce | deposits.md:188-189 | MUST | Regolith+ | ✅ | meta.deposit_nonce=preNonce（OpTransition.cpp:577-580） | Receipt.DepositNonce core/types/receipt.go:74-80 | 测试：SuccessMintsAndAdvancesNonce（nonce=5）OpDepositTest.cpp:81 |
| S-DEP-23 | 执行输出 gas 特殊边界（pre-Regolith：非 system tx 报 gasLimit、system tx 报 0） | deposits.md:163-166 | MUST | <Regolith | ➖ | FISCO 仅 Isthmus+（Regolith 为 baseline）；is_system_tx=true 直接块级错误（OpTransition.cpp:494-495）与 op-geth Regolith 行为一致 | pre-Regolith 分支 state_transition.go:617-629；Regolith 后 system tx 报错 :348-353 | 分叉不可达 |
| S-DEP-24 | 非 EOA 场景 CALLER/ORIGIN=from（tx.origin==msg.sender 不可用于 EOA 判定） | deposits.md:194-199 | —(说明) | Bedrock+ | ✅ | EVM 由 Host 以 tx.sender 提供 CALLER/ORIGIN（OpHost 构造传 tx，get_tx_context OpHost.cpp:71-107）；无额外处理 | EVMTxContext 同源 | 说明性条款 |
| S-DEP-25 | Nonce Handling：deposit 执行也递增 from nonce | deposits.md:203-207 | MUST | Bedrock+ | ✅ | 同 S-DEP-20 | 同 S-DEP-20 | |
| S-DEP-26 | Deposit receipt = 常规 receipt + depositNonce + depositReceiptVersion（Canyon 前必须省略，Canyon 起必须包含） | deposits.md:213-228 | MUST | Canyon+ | ✅ | runDeposit 恒写 nonce+version=1（OpTransition.cpp:577-580）；receipts-root 叶子编码 [status,cumGas,bloom,logs,nonce,version]（OpBlockExecute.cpp:244-276） | CanyonDepositReceiptVersion=1 receipt.go:52；RLP 可选字段 receipt.go:141-145 | 测试：OpReceiptEncodeTest.cpp:175-216（deposit 叶子编码） |
| S-DEP-27 | Regolith 起 receipt 的 cumulativeGasUsed 反映实际 gas usage | deposits.md:222,235 | MUST | Regolith+ | ✅ | decimalCumulative 按实际 gasUsed 累加（OpstackExecutor.h:621-624；processOpBlock.cpp:105-111） | Regolith 分支 UsedGas=st.gasUsed() state_transition.go:658 | |
| S-DEP-28 | L1 attributes deposit 字段值：from=0xdead…0001、to=0x4200…0015、mint=0、value=0、gasLimit=150M(pre-Regolith)/1M(after)、isSystemTx=true(pre)/false(after) | deposits.md:246-255 | MUST | Bedrock+ | ✅ | 构造侧在 op-node；EL 侧校验 to/from（isL1AttributesTx OpBlockExecute.h:91-94）；FISCO 自建 envelope 用 1M gas + isSystemTx=false（OpSchedulerSeam.h:116-131） | L1BlockAddr rollup_cost.go:68 | FISCO 端为消费+校验侧；构造侧判 ➖，此处 EL 校验判 ✅ |
| S-DEP-29 | L1 attributes calldata 为 setL1BlockValues() ABI 调用（Bedrock/Canyon/Delta） | deposits.md:264-268 | MUST | <Ecotone | ➖ | 分叉不可达（Ecotone+）；构造侧在 op-node | extractL1GasParamsPreEcotone rollup_cost.go:457-473 | |
| S-DEP-30 | L1Block predeploy 仅接受 depositor account 的状态修改调用（授权方案） | deposits.md:308-309 | MUST | Bedrock+ | ✅ | 授权逻辑在 L1Block.sol bytecode 内；FISCO 执行真实上游 bytecode（OpL1BlockRealBytecodeTest.cpp:284-436 全量 SSTORE 槽位与 selector 断言） | 合约层（contracts-bedrock） | 通过真实 bytecode 保真；测试：RealBytecodeSlotLayoutMatchesFeeParams |
| S-DEP-31 | L1Block 存储值：number/timestamp/basefee/hash/sequenceNumber/batcherHash/overhead/scalar | deposits.md:293-306 | MUST | Bedrock+ | ✅ | EL 消费侧按槽读取 1/3/7/8（OpFeeParams.cpp:36-46；布局 OpFeeParams.h:19-26） | 槽常量 rollup_cost.go:70-84 | |
| S-DEP-32 | 用户 deposit：isCreation→to=null；gasLimit 至少 21000；isSystemTx=false | deposits.md:333-344 | MUST | Bedrock+ | ✅ | 信封解码区分 to=nullopt（OpstackExecutor.h:327-341）；gas<21000 走失败分支（intrinsic 校验 OpTransition.cpp:520-521,529-536） | To=nil 语义 deposit_tx.go:35；deposit gas 由 intrinsic 校验 | 测试：EntryFailureChargesFullGasLimitButKeepsMint（gas 20999 失败）；ContractCreationHasNullTo OpDepositTxTest.cpp:10-27 |
| S-DEP-33 | 地址别名（L1 合约调用方 +0x1111…1111 溢出） | deposits.md:366-373 | MUST | Bedrock+ | ➖ | 属 OptimismPortal（L1 合约）职责，EL 不涉及 | — | 分工：L1 侧合约 |
| S-DEP-34 | deposits MUST 经信任路径进入（Engine API/可信 sync）；MUST never 从 tx pool 消费 | exec-engine.md:82-88 | MUST | Bedrock+ | ✅ | deposit 只从 payload 信封解码（depositFromTransaction OpstackExecutor.h:871-875，拒非 0x7e 信封）；无 tx pool 消费路径 | state_processor 仅从 payload 取 deposit | |
| S-DEP-35 | deposits 未签名、由 rollup node 认证（EL 不做签名验证） | exec-engine.md:79-81；deposits.md:141-144 | MUST | Bedrock+ | ✅ | DepositTx 无签名处理；decodeDepositEnvelope 显式无签名校验（OpstackExecutor.h:252-253 注释） | deposit_tx.go sigHash panic :90-92 | |

## S-L1A：l1-attributes 各分叉章节

| 编号 | 规范要点（原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-L1A-01 | Ecotone 激活块（非 genesis）L1 attributes 调 setL1BlockValues（合约未升级） | ecotone/l1-attributes.md:15-19 | MUST | Ecotone | ➖ | 构造侧（op-node）决策；EL 只执行信封 | FISCO 注释见 OpBlockExecute.h:76-78 | 分工：derivation/op-node |
| S-L1A-02 | 之后每块调 setL1BlockValuesEcotone；calldata 布局 164 字节（5×32B 段 + 6 段半填充）；总长 MUST 恰为 164 | ecotone/l1-attributes.md:20-43,119 | MUST | Ecotone | ➖ | 构造侧（op-node）；EL 消费侧从 L1Block 槽读参数不解析 calldata；测试按规范构建 164B calldata（OpFeeParamsTest.cpp:188-193,218-270） | extractL1GasParamsPostEcotone 要求 len==164 rollup_cost.go:478-503 | 分工：构造在 op-node |
| S-L1A-03 | 首个 Ecotone 后块：pre-Ecotone 值 1:1 迁移 + baseFeeScalar=旧 scalar、blobBaseFeeScalar=0、overhead 丢弃、blobBaseFee=origin blob fee（无 blob 支持则 1） | ecotone/l1-attributes.md:45-56 | MUST | Ecotone | ➖ | 构造侧（op-node）；EL 对 blobBaseFee=1 场景通过槽值消费（OpFeeParams.cpp:30） | op-geth L1 cost func 按槽消费 rollup_cost.go:165-167 | 分工：op-node |
| S-L1A-04 | blobBaseFee 须由 L1 origin 头按 EIP-4844 公式计算（BLOB_BASE_FEE_UPDATE_FRACTION 随 L1 分叉变化） | ecotone/l1-attributes.md:58-70 | MUST | Ecotone | ➖ | L1 侧数据，非 EL 职责（FISCO 无 L1 同步） | 同左（op-geth 亦由 op-node/L1 提供） | 分工：op-node（L1 数据来源） |
| S-L1A-05 | Ecotone L1Block 存储：新增 blobBaseFee 槽；baseFeeScalar/blobBaseFeeScalar 打包在 slot 3 | ecotone/l1-attributes.md:76-91,102-108 | MUST | Ecotone | ✅ | 槽 3 大端 [16,20)/[20,24) 解包（OpFeeParams.cpp:28-29）；slot 7=blobBaseFee（:30）；真实 bytecode 槽位测试（OpL1BlockRealBytecodeTest.cpp:322-336） | BaseFeeScalarSlotOffset=12 / BlobBaseFeeScalarSlotOffset=8 rollup_cost.go:38-39；L1BlobBaseFeeSlot=7 :76 | 测试：UnpacksScalarsFromPackedSlots OpFeeParamsTest.cpp:38-65；RealBytecodeSlotLayoutMatchesFeeParams :284+ |
| S-L1A-06 | Ecotone 后 overhead/scalar 仍可读但不再影响系统运行 | ecotone/l1-attributes.md:92-94 | — | Ecotone | ➖ | 合约侧保留槽 5/6；EL 不再消费（OpFeeParams 不含 overhead/scalar） | OverheadSlot=5/ScalarSlot=6 rollup_cost.go:71-72 | |
| S-L1A-07 | L1Block 随 Ecotone 升级到 1.2.0（新存储槽） | ecotone/l1-attributes.md:100-108 | MUST | Ecotone | ➖ | 合约部署/升级交易（genesis/upgrade automation），非 EL 执行语义 | — | 分工：合约层+op-node upgrade 交易 |
| S-L1A-08 | 激活后 setL1BlockValues 已弃用 MUST 永不调用；setL1BlockValuesEcotone MUST 被调用 | ecotone/l1-attributes.md:117-119 | MUST | Ecotone | ➖ | 构造侧约束；EL 与 op-geth 一样不校验 selector | op-geth 只按长度/selector 分支解析（rollup_cost.go:410-454） | 分工：op-node |
| S-L1A-09 | Isthmus L1 attributes：selector keccak256("setL1BlockValuesIsthmus()")[0:4]=0x098999be、176B 布局、新增 operatorFeeScalar(uint32)/operatorFeeConstant(uint64) | isthmus/l1-attributes.md:13-31 | MUST | Isthmus | ✅ | 消费侧：slot 8 大端 [20,24)/[24,32) 解包（OpFeeParams.cpp:31-32）；测试按规范 176B 构建（OpFeeParamsTest.cpp:271-300）与真实 bytecode 验证（OpL1BlockRealBytecodeTest.cpp:312,430-436） | IsthmusL1AttributesSelector rollup_cost.go:63；extractL1GasParamsPostIsthmus :507-543；ExtractOperatorFeeParams :656-660 | |
| S-L1A-10 | Isthmus 激活块用 setL1BlockValuesEcotone（合约未升级）；激活于 genesis 则无属性交易 | isthmus/l1-attributes.md:33-38 | MUST | Isthmus | ✅ | 激活块形态识别：176B attributes + deposits-only 强制（OpBlockExecute.cpp:36-44；OpBlockExecute.h:245-254）；fee 参数退化为槽读取 | 首块判断：selector 非 Ecotone 才走 Isthmus 分支 rollup_cost.go:411-415 | 测试：OpJovianShapeTest.cpp（176B 分支不校验 selector，与 op-geth 一致） |
| S-L1A-11 | 之后每块 setL1BlockValuesIsthmus 必须使用 | isthmus/l1-attributes.md:39 | MUST | Isthmus | ➖ | 构造侧（op-node） | — | |
| S-L1A-12 | Jovian L1 attributes：selector 0x3db6be2b、178B、新增 daFootprintGasScalar(uint16, [176:178]) | jovian/l1-attributes.md:13-32 | MUST | Jovian | ✅ | 消费侧：selector+长度校验（OpBlockExecute.cpp:47-54；OpBlockExecute.h:261-264）；scalar=calldata[176:178] 大端（OpBlockExecute.cpp:129-134, 270-279） | ExtractDAFootprintGasScalar rollup_cost.go:547-557；JovianL1AttributesSelector :65 | 测试：OpJovianShapeTest.cpp:122（错 selector 拒绝）；JovianL1CostBlobScalarAnchor RollupCostTest.cpp:282 |
| S-L1A-13 | daFootprintGasScalar=0 时默认 400 | jovian/l1-attributes.md:43-44 | MUST | Jovian | ➖ | 构造侧（op-node SystemConfig 读取）；FISCO 无该逻辑（grep 无 400 默认值） | 同左（op-geth Extract 侧不设默认） | 分工：op-node 构造；若 FISCO 自建 envelope（OpSchedulerSeam.h:122-131）scalar 恒 0，fixture 场景可接受 |
| S-L1A-14 | Jovian 激活块：setL1BlockValuesIsthmus 必须使用；若 genesis 激活则无属性交易 | jovian/l1-attributes.md:34-38 | MUST | Jovian | ✅ | 176B 激活块 deposits-only + DA scalar=0（OpBlockExecute.cpp:36-44,127-128；OpBlockExecute.h:245-254,270-274） | CalcDAFootprint 激活块分支 rollup_cost.go:568-577 | |

## S-FEE：费用条款

| 编号 | 规范要点（原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-FEE-01 | 1559 参数：per-chain denominator/elasticity；Canyon 起用 EIP1559DenominatorCanyon | exec-engine.md:43-51 | MUST | Bedrock+ | 🟡 | FISCO 执行层不计算 base fee——base_fee 直接取自 header（OpCommon.h:190-192），无 denominator/elasticity 配置与公式实现 | CalcBaseFee + BaseFeeChangeDenominator consensus/misc/eip1559/eip1559.go:73-100 | 偏差：区块构建/验证侧缺失；header.baseFee 完全由外部 payload 提供，无法校验其正确性 |
| S-FEE-02 | 三种费用（priority/base/L1）收集到 3 个 fee vault（硬编码地址+proxy） | exec-engine.md:92-107 | MUST | Bedrock+ | ✅ | 地址常量（OpPredeploys.h:14-18）；opTransition 分账（OpTransition.cpp:315,327-329） | 地址 params/protocol_params.go:27-33 | 测试：RoutesFeesToFourVaults OpTransitionTest.cpp:39-97 |
| S-FEE-03 | priority fee 遵循 1559，归区块 fee-recipient（=SequencerFeeVault） | exec-engine.md:109-112 | MUST | Bedrock+ | ✅ | gas_used×priority_gas_price → coinbase（OpTransition.cpp:315）；区块 coinbase 设为 SequencerFeeVault（调度/测试 OpTransitionTest.cpp:57） | AddBalance(coinbase, fee) state_transition.go:676-678 | 测试：RoutesFeesToFourVaults:95 |
| S-FEE-04 | base fee 不 burn，累入 BaseFeeVault | exec-engine.md:114-117 | MUST | Bedrock+ | ✅ | gas_used×base_fee → OP_BASE_FEE_VAULT（OpTransition.cpp:327-328） | AddBalance(OptimismBaseFeeRecipient) state_transition.go:687-693 | 测试：RoutesFeesToFourVaults:93；OpZeroDiffTest:169-173 |
| S-FEE-05 | L1-cost fee 从 sender 扣、进 L1FeeVault | exec-engine.md:119-123 | MUST | Bedrock+ | ✅ | sender 扣 l1_cost（OpTransition.cpp:304）+ 入 OP_L1_FEE_VAULT（:329） | AddBalance(OptimismL1FeeRecipient) state_transition.go:694-700 | 测试：L1CostIsDebitedFromSenderAndConserves OpTransitionTest.cpp:481+ |
| S-FEE-06 | Pre-Ecotone L1 cost：`(rollupDataGas+overhead)*baseFee*scalar/1e6`；pre-Regolith 加 68 非零字节 | exec-engine.md:128-147 | MUST | <Ecotone | ➖ | 分叉不可达（FISCO Ecotone+）；未实现 | newL1CostFuncBedrock rollup_cost.go:291-317 | |
| S-FEE-07 | Ecotone L1 cost：`(zeroes*4+ones*16)*(16*baseFee*baseScalar+blobBaseFee*blobScalar)/16e6`（unlimited precision） | exec-engine.md:163-190 | MUST | Ecotone | ✅ | computeL1Cost Ecotone 分支（RollupCost.cpp:187-201）；512-bit 中间计算防 wrap（:191-200） | newL1CostFuncEcotone rollup_cost.go:321-348 | 测试：EcotoneL1DiffersFromFjordSameEnvelope RollupCostTest.cpp:104+；EcotoneCalldataRoundTrip OpFeeParamsTest.cpp:218 |
| S-FEE-08 | Ecotone 参数两种读取：L1 attributes / L1Block 槽（basefee slot1、blobBaseFee slot7、scalars slot3 偏移 12/8） | exec-engine.md:194-204 | MUST | Ecotone | ✅ | loadOpFeeParams 读槽 1/3/7（OpFeeParams.cpp:36-46）；slot3 大端 [16,20)/[20,24)（OpFeeParams.cpp:28-29） | 槽常量 rollup_cost.go:70-84；ExtractEcotoneFeeParams :649-654 | 测试：UnpacksScalarsFromPackedSlots；LoadFromStateEqualsManualUnpack OpFeeParamsTest.cpp:66-92 |
| S-FEE-09 | Fjord L1 cost：`estimatedSizeScaled=max(100e6, -42585600+836500*fastlzSize)`；`l1Fee=estimatedSizeScaled*l1FeeScaled/1e12` | fjord/exec-engine.md:22-43 | MUST | Fjord | ✅ | 常量（RollupCost.cpp:12-15）；computeL1CostFromFlz（:158-179）；estimatedDaSizeScaled（:126-131） | 常量 rollup_cost.go:92-96；NewL1CostFuncFjord :608-628 | 测试：FjordL1CostEmptyTxMatches3203000 RollupCostTest.cpp:89-95；EstimatedDaSizeFloorsToMinimum :76-82 |
| S-FEE-10 | 压缩算法必须等价于 fastlz_compress（指定 commit） | fjord/exec-engine.md:49-52 | MUST | Fjord | ✅ | flzCompressLen 移植（RollupCost.cpp:26-118）；向量测试与 op-geth 逐字节对齐 | FlzCompressLen rollup_cost.go:669-743 | 测试：FlzCompressLenMatchesOpGethVectors RollupCostTest.cpp:65-75。注：FISCO 与 op-geth 均为输出长度模拟（非完整压缩输出），二者一致 |
| S-FEE-11 | L1GasUsed 属性已弃用（不捕获 blob gas），L1Fee 继续可用 | fjord/exec-engine.md:71-75 | — | Fjord+ | ✅ | receipt 仍输出 l1_gas_used（Fjord 公式 estimatedDaSizeScaled*16/1e6，OpTransition.cpp:245-249），与 op-geth Fjord 一致 | L1GasUsed = estimatedDASize*16/1e6 rollup_cost.go:623-624；receipt_opstack.go:36 | 弃用仅 API 层提示，字段保留 |
| S-FEE-12 | Isthmus operator fee：`gas*scalar/1e6+constant` | isthmus/exec-engine.md:220-232 | MUST | Isthmus | ✅ | computeOperatorCost Isthmus 分支（RollupCost.cpp:217-219） | newOperatorCostFuncIsthmus rollup_cost.go:254-269 | 测试：OperatorCostIsthmus RollupCostTest.cpp:164-170 |
| S-FEE-13 | operator fee 最大 77 bits，uint256 计算无需溢出检查 | isthmus/exec-engine.md:234-241 | MUST | Isthmus | ✅ | intx::uint256 全程计算（RollupCost.cpp:208-220） | uint256.FromBig + panic 保护 rollup_cost.go:261-265 | 测试：OperatorFeeMaxValuesNoWrap RollupCostTest.cpp:243-252 |
| S-FEE-14 | deposit 不收取 operator fee（恒 0），也无 operator fee 退款 | isthmus/exec-engine.md:243-247 | MUST | Isthmus | ✅ | runDeposit 无任何 operator 扣费/退款路径（OpTransition.cpp:488-582 无 has_operator_fee 分支） | 仅非 deposit 触发 OperatorCostFunc（state_transition.go:701-707） | 测试：SuccessMintsAndAdvancesNonce 无 vault 触碰 |
| S-FEE-15 | 预执行校验：余额须覆盖 worst-case gas+L1 数据费+worst-case operator fee（deposit 为 0） | isthmus/exec-engine.md:251-256 | MUST | Isthmus | ✅ | opValidate 512-bit 余额 cap = gasLimit*maxGasPrice+value+l1Cost+opCost(gasLimit)（OpTransition.cpp:412-421） | buyGas balanceCheck（state_transition.go:294-325） | 测试：opValidate_zero_fee_passes_balance_check OpZeroFeeSpikeTest.cpp:88-104 |
| S-FEE-16 | 买 gas 时收取 worst-case operator fee（gas=gas_limit） | isthmus/exec-engine.md:257-258 | MUST | Isthmus | ✅ | sender 预扣 operator_cost_at_gas_limit（OpTransition.cpp:305-306） | buyGas mgval += OperatorCostFunc(gasLimit) state_transition.go:289-292 | |
| S-FEE-17 | 执行后退还未用 operator fee（仅收实际费用） | isthmus/exec-engine.md:259-265 | MUST | Isthmus | ✅ | 退款 cap-used（OpTransition.cpp:330-335）；formula 用 refund 后 gas_used（:323-326） | refundIsthmusOperatorCost state_transition.go:812-822 | 测试：OperatorFeeConservesWhenCfgDisagreesWithProps OpTransitionTest.cpp:337+ |
| S-FEE-18 | 奖励阶段：实际 operator fee 送 OperatorFeeVault | isthmus/exec-engine.md:266-267 | MUST | Isthmus | ✅ | OP_OPERATOR_FEE_VAULT 入账（OpTransition.cpp:330-332；地址 OpPredeploys.h:19-20） | AddBalance(OptimismOperatorFeeRecipient) state_transition.go:701-707 | 测试：RoutesFeesToFourVaults:96；JovianReceiptMetaAndOperatorFormula:223 |
| S-FEE-19 | operator fee 不得凭空铸造或销毁 ETH | isthmus/exec-engine.md:269 | MUST | Isthmus | ✅ | 扣/退/入账三边同公式守恒（OpTransition.cpp:305-306,323-335） | 同源守恒（refund 公式）state_transition.go:812-822 | 测试：totalSupply 守恒断言 OpTransitionTest.cpp:25-34,337+ |
| S-FEE-20 | txpool 必须拒绝余额不足 worst-case 费用的交易 | isthmus/exec-engine.md:271-275 | MUST | Isthmus | 🟡 | FISCO 无 txpool 组件；执行路径有等价 512-bit cap（OpTransition.cpp:412-421） | NewTotalRollupCostFunc（txpool 用）rollup_cost.go:353-389 | 偏差：txpool 准入未实现；因 FISCO 为验证器执行路径，影响低 |
| S-FEE-21 | operatorFeeScalar/Constant 读取：L1 attributes / L1Block getter / slot 8 直读（uint32@offset0、uint64@offset4） | isthmus/exec-engine.md:277-288 | MUST | Isthmus | ✅ | slot 8 大端 [20,24)/[24,32)（OpFeeParams.cpp:31-32）与 op-geth 读取一致 | ExtractOperatorFeeParams（[20:24]/[24:32]）rollup_cost.go:656-660 | 注：规范"offset 0/4"与 op-geth/FISCO 的 [20:24]/[24:32] 存在表述差异，但 FISCO 与 op-geth 字节一致；真实 bytecode 测试锁定（OpL1BlockRealBytecodeTest.cpp:430-436） |
| S-FEE-22 | OperatorFeeVault 为硬编码地址 + FeeVault 代理部署 | isthmus/exec-engine.md:290-295 | MUST | Isthmus | ✅ | 地址常量（OpPredeploys.h:19-20）；proxy/vault 合约在 predeploy seed | 地址 protocol_params.go:33 | 测试：OpPredeploysTest 覆盖地址种子 |
| S-FEE-23 | Isthmus 起 receipt 新增 operatorFeeScalar/Constant，仅当至少一个非零时包含 | isthmus/exec-engine.md:297-300 | MUST | Isthmus | ✅ | deriveOpReceiptMeta 条件填充（OpTransition.cpp:253-258） | receipt_opstack.go:38-41 | 测试：ReceiptCarriesL1AndOperatorMeta OpTransitionTest.cpp:99-157 |
| S-FEE-24 | Jovian operator fee：`gas*scalar*100+constant`（有效每 gas 标量 ×100） | jovian/exec-engine.md:165-175 | MUST | Jovian | ✅ | kJovianOperatorFeeMultiplier=100（RollupCost.cpp:23）；jovian 分支（:211-215） | newOperatorCostFuncOperatorFeeFix rollup_cost.go:272-287 | 测试：OperatorCostJovianUsesTimes100 RollupCostTest.cpp:171-183；JovianReceiptMetaAndOperatorFormula OpTransitionTest.cpp:158-225 |
| S-FEE-25 | Jovian operator fee 最大 103 bits，uint256 无需溢出检查 | jovian/exec-engine.md:177-186 | MUST | Jovian | ✅ | intx::uint256（RollupCost.cpp:213-215） | rollup_cost.go:279-283 | 测试：OperatorFeeMaxValuesNoWrap（含 Jovian 分支） |
| S-FEE-26 | DA footprint 定义：跳过 deposit；`daUsage=max(minTxSize,(intercept+coef*flz)//1e6)*scalar`，块级求和 | jovian/exec-engine.md:98-119 | MUST | Jovian | ✅ | per-tx：deriveOpReceiptMeta（OpTransition.cpp:260-265，estimatedDaSizeFromFlz×scalar）；块级：sealOpBlock 求和（OpBlockExecute.cpp:321-331） | CalcDAFootprint rollup_cost.go:563-591；EstimatedDASize :644-647 | 测试：JovianReceiptMetaAndOperatorFormula:217-220（da_footprint=estimatedDaSize×2） |
| S-FEE-27 | Jovian 起 header blobGasUsed = 块 daFootprint（Ecotone 起本为 0，被复用） | jovian/exec-engine.md:121-122 | MUST | Jovian | ✅ | seal.blobGasUsed=footprint（OpBlockExecute.cpp:321-331）→ 写入 executed header（OpScheduler.h:732-733）；与 payload 宣告值对比校验（OpCommitments.h:103-105） | 同 S-FEE-26 | |
| S-FEE-28 | 块构建/验证须保证并检查块 daFootprint < gasLimit | jovian/exec-engine.md:124-126 | MUST | Jovian | 🟡 | FISCO 计算+写入+与 payload 对比（OpCommitments.h:103-105），但无显式 `footprint<=gasLimit` 检查；op-geth 亦无显式检查（构建侧约束） | 构建侧约束（miner 打包）；无验证端显式检查 | 与 op-geth 行为一致；"保证"责任在构建方（op-node） |
| S-FEE-29 | Jovian 起 base fee 更新用 `gasMetered=max(gasUsed, blobGasUsed)` | jovian/exec-engine.md:128-130 | MUST | Jovian | ❌ | FISCO 无 base fee 计算/更新逻辑（base_fee 仅从 header 读取 OpCommon.h:190-192）；未接线 | calcBaseFeeInner Jovian 分支 consensus/misc/eip1559/eip1559.go:99-107 | 影响：无法复现/校验 Jovian 动态 base fee（含 DA 驱动上涨）；依赖外部 header |
| S-FEE-30 | Jovian receipt 新增 daFootprintGasScalar；blobGasUsed receipt 字段=交易 DA footprint | jovian/exec-engine.md:145-149 | MUST | Jovian | ✅ | deriveOpReceiptMeta（OpTransition.cpp:260-265） | receipt_opstack.go:45-50 | 测试：JovianReceiptMetaAndOperatorFormula:217-220 |
| S-FEE-31 | DA scalar 加载两种方式：L1 attributes calldata（jovian schema）/ L1Block 槽 8 大端 uint16@offset12 | jovian/exec-engine.md:132-143 | MUST | Jovian | ✅ | 双路径：calldata[176:178]（OpBlockExecute.cpp:129-134, 270-279）+ slot 8 大端 [18,20)（OpFeeParams.cpp:33） | ExtractDAFootprintGasScalar rollup_cost.go:547-557 | 测试：UnpacksDaFootprintGasScalarFromSlot8 OpFeeParamsTest.cpp:93-120；H1c 覆盖 OpSchedulerTest |
| S-FEE-32 | Holocene 动态 1559：header extraData 必须为 9B（version=0、denominator/elasticity 非零、无多余数据）；base fee 计算取 parent extraData | holocene/exec-engine.md:35-53,101-111 | MUST | Holocene | ❌ | FISCO opstack 执行层无 extraData 解码/校验/参与 base fee（grep OpScheduler.h/OpCommon.h 无 EIP1559 参数解析；extra_data 仅透传 OpCommon.h:198） | Validate/Decode/EncodeOptimismExtraData consensus/misc/eip1559/eip1559_optimism.go:17-45；CalcBaseFee 解码 eip1559.go:82-90 | 影响：Holocene 动态 1559 参数（denominator/elasticity）既不解码也不校验，base fee 完全由外部 payload 提供；若该部分归 block/header 验证子任务，请协调复核 |
| S-FEE-33 | Jovian minBaseFee：extraData 17B（version=1、+minBaseFee u64 BE [9,17)）；计算 baseFee<minBaseFee 时 clamp | jovian/exec-engine.md:32-57 | MUST | Jovian | ❌ | 无实现（同 S-FEE-32；grep 无 minBaseFee） | DecodeJovianExtraData/EncodeJovianExtraData eip1559_optimism.go:47-56；clamp eip1559.go:92-98 | 影响：min base fee 不生效，链条 base fee 可低于配置下限 |
| S-FEE-34 | Holocene 起 PayloadAttributesV3 eip1559Params 8B（denominator/elasticity BE）；Holocene 前必须 null | holocene/exec-engine.md:55-99 | MUST | Holocene | ❌ | FISCO 无 PayloadAttributes/eip1559Params 解析（engine API 侧未实现；payload 直接解包为 header 字段） | DecodeHolocene1559Params/ValidateHolocene1559Params eip1559_optimism.go:17-45 | 影响同 S-FEE-32；engine API 参数验证缺失 |
| S-FEE-35 | Ecotone：禁用 blob 交易（网络/池/构建/状态转移四层）；BLOBBASEFEE 恒 push 1 | exec-engine.md:473-490 | MUST | Ecotone | ✅ | blob 类型拒绝：opValidate（OpTransition.cpp:379-381）+ payload 信封白名单 {0x7e,0x01,0x02,0x04,≥0xc0}（OpScheduler.h:616-624）；BLOBBASEFEE=1（OpHost.cpp:99-101） | 状态转移 blob 分支不存在于 OP；BLOBBASEFEE 语义由 L2 无 blob 保证 | 注：FISCO 信封白名单比 op-geth 更严格（op-geth 接受 0x03 信封）——方向一致的保守拒绝，DIVERGENCES 已记录（OpScheduler.h:617-623 注释） |
| S-FEE-36 | L1 费用参数读取时机：须在 L1 attributes deposit 执行后才读（consensus-critical 懒加载） | exec-engine.md:163-164（op-geth 注释语义）；isthmus/exec-engine.md:277-288 | MUST | Ecotone+ | ✅ | fee 懒加载于第一个 normal tx（deposit 已执行）：OpstackExecutor.h:557-565；processOpBlock.cpp:118-123 | "not initialized from the DB until this point to allow deposit transactions … processed first" rollup_cost.go:162-164 | 测试：OpSchedulerTest H1/H1c 场景 |
| S-FEE-37 | Pre-Ecotone 费用参数槽读取（slot1 basefee、slot5 overhead、slot6 scalar） | exec-engine.md:152-161 | MUST | <Ecotone | ➖ | 分叉不可达；OpFeeParams 无 overhead/scalar | newL1CostFuncBedrock rollup_cost.go:291-297 | |

## 跨分叉复述条款去重说明

- Regolith 相关（isSystemTx 禁用、实际 gas 记账、depositNonce、L1 cost 修正 +68 移除）已在 S-DEP-13/21/22/23/27 与 S-FEE-06 中合并表述（regolith/overview.md:20-32）；`+68` 仅存在于 pre-Regolith（S-FEE-06 不可达）。
- Canyon 相关（depositNonce/depositReceiptVersion 入 receipt hash）已在 S-DEP-26 表述（canyon/overview.md:41）。
- ecotone/overview.md 与 fjord/overview.md 无独立费用条款（均为本表 S-FEE-07/09 的指引）。

## 判定汇总

| 类别 | 总数 | ✅ | 🟡 | ❌ | ➖ |
|---|---|---|---|---|---|
| S-DEP | 35 | 32 | 0 | 0 | 3 |
| S-L1A | 14 | 6 | 0 | 0 | 8 |
| S-FEE | 37 | 28 | 3 | 3 | 3 |
| **合计** | **86** | **66** | **3** | **3** | **14** |

## 重点差距清单

1. **S-FEE-29 / S-FEE-32 / S-FEE-33 / S-FEE-34（❌，Holocene/Jovian 动态 1559 + minBaseFee）**：FISCO opstack 执行层完全没有 base fee 计算、extraData 参数解码/校验与 minBaseFee clamp——base_fee 直接取自外部 payload header（OpCommon.h:190-192），执行层无法校验构建方提供的 baseFee 是否按规范（含 Jovian gasMetered=max(gasUsed,blobGasUsed) 与 minBaseFee 下限）得出。影响：动态 1559 与 minimum base fee 特性未接线，接外部 op-node payload 时依赖对方正确性（fixture 链当前自洽，但非规范对齐）。参照：op-geth consensus/misc/eip1559/eip1559.go:73-107 + eip1559_optimism.go:17-56。注意：若 header/engine-API 验证归其他子任务，请协调归属，避免双计。
2. **S-FEE-20（🟡，txpool worst-case 拒绝）**：isthmus/exec-engine.md:271-275 要求 txpool 拒绝余额不足交易；FISCO 无 txpool 组件，仅执行路径有等价 512-bit cap（OpTransition.cpp:412-421）。影响：无池准入，sequencer 场景缺位；验证器执行路径不受影响。
3. **S-FEE-01（🟡，1559 参数/Canyon denominator）**：FISCO 不实现 base fee 公式与 denominator 配置（同 1 的根因），base fee 全部来自 header。影响：与 S-FEE-29/32/33 同源。
4. **S-FEE-28（🟡，DA footprint 块上限检查）**：FISCO 计算并写入 blobGasUsed 且与 payload 宣告值比对（OpCommitments.h:103-105），但无显式 `daFootprint <= gasLimit` 检查；op-geth 同样无显式验证（构建侧约束）。影响：低——与参照实现行为一致，规范"保证"责任在构建方。
5. **S-L1A-13（➖ 分工但值得注意）**：daFootprintGasScalar=0→400 默认值在 op-node 构造侧；FISCO 自建 envelope（OpSchedulerSeam.h:116-131）scalar 恒 0 且无默认化——fixture 链 DA 计费为 0 的已知行为，接真实 op-node 后由对方保证。

## 存疑点（需协调/复核）

- **deposit receipt 的 L1/operator/DA 字段来源**：op-geth 的 receipt 字段（L1Fee/L1BaseFeeScalar/operatorFeeScalar 等）从 L1 attributes **calldata** 解析（extractL1GasParams, rollup_cost.go:410-454），FISCO 从 **L1Block 存储槽**读取（loadOpFeeParams）。正常情况下两者一致（attributes deposit 将 calldata 写入槽）；差异仅当 attributes deposit 执行失败（槽不更新）时出现——此时 op-geth receipt 用 calldata 值、FISCO 用旧槽值。属低概率共识差异风险，建议确认 op-geth 该路径的确定性（op-node 保证 attributes deposit 不失败）。
- **Jovian 激活块判定**：FISCO 与 op-geth 均以"176B 长度"而非 Isthmus selector 识别激活块（OpBlockExecute.cpp:36-44 vs rollup_cost.go:571-577），规范 isthmus/l1-attributes.md:36-37 要求用 setL1BlockValuesEcotone——两参照实现一致采用长度启发式，规范原文（selector 区分）未被实现，建议确认规范意图。
- **S-DEP-05 内容检查降级**：FISCO 对"首笔是 deposit 但非 L1-attributes tx"仅记 WARNING（OpBlockExecute.h:240-244），与 op-geth/op-reth 验证端一致，但严于规范 MUST（deposits.md:131）字面；若后续要求严格对齐规范字面，需恢复硬拒绝（会造成与 op-geth 验证端的分叉）。
- **is_system_tx=true（Regolith+）**：FISCO 抛块级错误（OpTransition.cpp:494-495，对应 op-geth ErrSystemTxNotSupported），与 op-geth 一致；规范 deposits.md:77 仅注"boolean disabled starting from Regolith"，未明示块级错误，两参照一致故判 ✅。


## 模块：m2-withdrawals-precompiles

# M2 审计表：withdrawals / precompiles / predeploys / preinstalls / system-config（EL 侧）

- 模块：M2（OP Stack EL spec 对齐审计子任务）
- 负责章节：`specs/protocol/{withdrawals,precompiles,predeploys,preinstalls,system-config}.md`；`fjord/predeploys.md`；`isthmus/{predeploys,system-config}.md`；`jovian/system-config.md`；`holocene/system-config.md`（system-config 仅取 EL 相关；op-node 侧职责记 ➖）
- 规范基线 commit：2049036afe878a7cb443f513f4e6ca453d90c340（/tmp/op-specs）
- 参照实现 op-geth commit：d0734fd5f44234cde3b0a7c4beb1256fc6feedef（optimism 分支本地源码 /Users/octopus/octo/code/op-geth）
- FISCO 基线：Isthmus 默认 + Jovian（feature_op_jovian 开关，OpForkSchedule.cpp:103-110）；无 Canyon/Holocene 独立分叉路径（语义内建于创世与基线）
- 取证日期：2026-08-21；测试证据来自已构建二进制（bcos-evm-opstack-tests / opstack-executor-block-tests），全部通过

判定图例：✅ 符合；🟡 部分符合；❌ 缺失；➖ 不适用（非 EL 责任，注明分工依据）

## S-WDL — Withdrawals

| 编号 | 规范要点（英文原文关键句+中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-WDL-1 | "Withdrawals are initiated on L2 via a call to the Message Passer predeploy contract, which records the important properties of the message in its storage."（提款由 L2ToL1MessagePasser 预部署合约发起并记录消息） | withdrawals.md:45-46, 61-62, 87-89 | MUST | 全分叉（Bedrock+） | ✅ | 地址常量 OP_L2_TO_L1_MESSAGE_PASSER=0x4200…16（OpPredeploys.h:26-27）；创世注入：chain-config.yaml:31-33（vendored L2ToL1MessagePasser）+ base allocs 中 0x…16 带 code（op-fork-base-allocs.json，脚本验证）；EL 侧以该账户存储根出 header：OpBlockExecute.cpp:220-242, 307-311 | params/protocol_params.go:31（OptimismL2ToL1MessagePasser 常量） | 记录逻辑在合约字节码内（非 EL 代码），EL 职责=创世注入该地址合约 + 计算其存储根（见 S-WDL-7） |
| S-WDL-2 | "The `MessagePassed` event includes all of the data that is hashed and stored in the `sentMessages` mapping, as well as the hash itself."（MessagePassed 事件/initiateWithdrawal/messageNonce/sentMessages/burn 接口） | withdrawals.md:91-117 | MUST | 全分叉 | ➖ | — | — | 合约级接口（Solidity），L1 证明侧依赖；EL 不实现。注入证据同 S-WDL-1 |
| S-WDL-3 | "Addresses are not Aliased on Withdrawals … withdrawals, which do not modify the sender's address"（提款不改写 sender，无地址别名） | withdrawals.md:119-130 | MUST | 全分叉 | ✅ | 提款/普通交易路径无任何 sender 改写逻辑：OpTransition.cpp:304-334 仅扣费与 vault 入账（coinbase/L1/operator/base fee），sender 原样执行 | 规范级语义；op-geth 别名仅存在于 deposit 处理（属 deposits 章节） | 别名只对 deposit 生效；FISCO 全链无提款别名代码路径 |
| S-WDL-4 | "Withdrawals are proven on L1 via a call to the `OptimismPortal` … finalized on L1 via a call to the `OptimismPortal` contract"（L1 侧 prove/finalizeWithdrawalTransaction/l2Sender 流程） | withdrawals.md:47-49, 66-83, 132-160 | MUST | 全分叉 | ➖ | — | — | L1 合约职责（OptimismPortal/L2OutputOracle），EL 无角色 |
| S-WDL-5 | "These inputs must satisfy the following conditions: … keccak256 hash of the `outputRootProof` values is equal to the `outputRoot` … valid inclusion proof …"（prove 验证条件：outputRoot keccak 相等、存储包含证明等） | withdrawals.md:178-184 | MUST | 全分叉 | ➖ | — | — | L1 合约验证逻辑（含 L2ToL1MessagePasser 存储证明），EL 无角色 |
| S-WDL-6 | "It should only be possible to prove the withdrawal once … finalize the withdrawal once … It should not be possible to relay the message with any of its fields modified … we have not provided any replay functionality"（防双花/一次性证明与终结/字段不可改/无 replay） | withdrawals.md:190-212 | MUST | 全分叉 | ➖ | — | — | L1 合约+中继器职责；EL 侧依赖 L2 端 hash 提交（合约） |
| S-WDL-7 | "After Isthmus hardfork's activation, the L2 block header's `withdrawalsRoot` field will consist of the 32-byte L2ToL1MessagePasser account storage root … The `requestsHash` field is equal to `sha256('')` = 0xe3b0c4…"（Isthmus 起 header.withdrawalsRoot=MessagePasser 存储根；requestsHash=sha256("")） | isthmus/exec-engine.md:57-66, 79-81（isthmus/exec-engine.md 属 Isthmus 章节，本模块交叉引用） | MUST | Isthmus+ | ✅ | sealOpBlock：Isthmus+ 时 seal.withdrawalsRoot=opStorageRoot(messagePasserStorage)、seal.requestsHash=OP_EMPTY_REQUESTS_HASH（OpBlockExecute.cpp:305-311；空请求哈希常量 OpBlockExecute.h:103-105）；post-finalize passer 快照采集 finalizeOpBlockResult（OpBlockExecute.h:169-181）；承诺比较含 withdrawalsRoot/requestsHash（OpCommitments.h:92-93, 106-108）；验证侧要求 payload 必带 withdrawalsRoot（EngineServiceImpl.cpp:413-417）。测试：OpL1BlockDepositTest.cpp:780-866（MessagePasserStorageDrivesWithdrawalRoot 实测 seal 根=opStorageRoot 0x6c8ac9…；EmptyPasserStorageSealsEmptyRootConstant 钉死空根 0x56e81f…；WithdrawTxWritesMessagePasserAndChangesRoot:901）— 已构建二进制运行通过 | core/block_validator.go:191-196（ValidateState：header.WithdrawalsHash == statedb.GetStorageRoot(params.OptimismL2ToL1MessagePasser)）；:80-83（Isthmus 起块体 withdrawals 列表必须为空）；core/genesis.go:184-190（Isthmus 创世 withdrawalsRoot=passer 存储根） | 语义与 eth_getProof 存储根一致；FISCO 用 pass storage 的 keccak-secure-trie 根（opStorageRoot）等价 |
| S-WDL-8 | "Prior to isthmus activation: the withdrawalsRoot field must be keccak256(rlp(empty)) if Canyon activated … the hash of the withdrawals list to be the MPT root of an empty list"（Canyon≤fork<Isthmus：withdrawalsRoot=空列表 MPT 根，withdrawals 列表恒空） | isthmus/exec-engine.md:66-77, 108-122 | MUST | Canyon 起、Isthmus 前 | ✅ | 不可达路径但实现存在：非 Isthmus 分支写空 trie 根（OpBlockExecute.cpp:312-317），空根常量与 op-geth EmptyWithdrawalsHash 一致（测试 EmptyPasserStorageSealsEmptyRootConstant 钉死 0x56e81f…）；验证侧强制 OP 路径 withdrawals 列表为空（EngineServiceImpl.cpp:403） | core/block_validator.go:75-87（Canyon 后 WithdrawalsHash=DeriveSha(空列表)） | FISCO 最小循环仅 Isthmus+（configAt 只返回 isthmus/jovian，OpForkSchedule.cpp:103-110），该分支为防御性死代码 |

## S-PRE — Precompiles

| 编号 | 规范要点（英文原文关键句+中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-PRE-1 | "OP-Stack chains contain the standard Ethereum precompiles … as well as … P256VERIFY … Address 0x0000…0100 … Fjord"（OP 链=标准 EVM 预编译 + P256VERIFY@0x100，Fjord 引入） | precompiles.md:19-25, 36 | MUST | Fjord+ | ✅ | kP256VerifyAddress{0x100}（OpPrecompiles.h:13）；kFjordEntries（OpPrecompiles.cpp:45-47）；fjord/granite/holocene/isthmus/jovian 全部接线（OpForkSchedule.cpp:26, 41, 52, 63, 78）；测试 OpPrecompilesTest.cpp:10-19 + OpHostSuite/OverrideTableInterceptsP256 — 已运行通过 | core/vm/contracts.go:193/209/230/251（0x0100→p256VerifyFjord 从 Fjord 起）；:176-177 | 标准 EVM 预编译由 evmone 提供 |
| S-PRE-2 | "The `P256VERIFY` precompile performs signature verification for the secp256r1 elliptic curve … specified as part of RIP-7212"（secp256r1 验签，输入 160 字节，RIP-7212 语义） | precompiles.md:27-34 | MUST | Fjord+ | ✅ | OpHost::call 派发 0x100 → executeGasOverridePrecompile → evmone::state::p256verify_execute（OpHost.cpp:20-46, 109-161）；EIP-7702 委托路径豁免（OpHost.cpp:120-121）；测试 OpHostTest.cpp:65-110（CallToP256EmptyAccountIsNotSilentSuccess / CallToP256TransfersValue）— 已运行通过 | core/vm/contracts.go:1717-1735（p256Verify.Run：len(input)==160） | 实现委托 evmone（与 op-geth 独立实现行为对齐） |
| S-PRE-3 | "P256VERIFY … 3450 gas"（Fjord 起 P256VERIFY 固定 3450 gas，非默认 6900） | precompiles.md:29-34；protocol_params 常量见 op-geth | MUST | Fjord+ | ✅ | gas_cost_override=3450（OpPrecompiles.cpp:29 [isthmus/jovian], 46 [fjord/granite]）；gas 不足返 EVMC_OUT_OF_GAS（OpHost.cpp:23-24）；测试 OpPrecompilesTest.cpp:17 + OpHostTest CallToP256EmptyAccountIsNotSilentSuccess（gas_left=msg.gas-3450）— 已运行通过 | params/protocol_params.go:181（P256VerifyGasFjord=3450）；core/vm/contracts.go:1703-1704（p256VerifyFjord.RequiredGas） | op-geth 默认 P256VerifyGas=6900 仅用于非 OP 路径（:182） |
| S-PRE-4 | 输入长度上限（Granite/Isthmus 收紧）：bn256Pairing 112,687B（Granite，Isthmus 沿用）；BLS12-381 G1/G2 MSM、pairing 513,760 / 488,448 / 235,008B（Isthmus）（"Precompile Input Size Restrictions"） | granite/isthmus exec-engine.md（本模块章节外，交叉引用；FISCO 注释源 params/protocol_params.go:172,186-188） | MUST | Granite+ / Isthmus+ | ✅ | kGraniteEntries/kIsthmusEntries（OpPrecompiles.cpp:27-33, 49-52）；超限返 EVMC_FAILURE（OpHost.cpp:123-124）；测试 OpPrecompilesTest.cpp:21-56 + OpHostSuite/JovianBn256PairingInputAtLimitExecutes — 已运行通过 | core/vm/contracts.go:940（bn256Pairing Granite 限）, 1120/1267/1369（BLS Isthmus 限）；params/protocol_params.go:170, 186-188 | 长度限制只作用于 MSM/pairing；G1Add/G2Add/Map 无限制（OpPrecompiles.cpp:24-26 注释） |
| S-PRE-5 | Jovian 重新收紧输入上限：bn256Pairing 81,984B；BLS G1/G2 MSM、pairing 288,960 / 278,784 / 156,672B（"The new input size restrictions are: …"） | jovian/exec-engine.md#precompile-input-size-restrictions（本模块章节外，交叉引用） | MUST | Jovian+ | ✅ | kJovianEntries（OpPrecompiles.cpp:35-41）；超限 EVMC_FAILURE（OpHost.cpp:123-124）；测试 OpPrecompilesTest.cpp:63-80（JovianLimitsStricterThanIsthmus）+ OpHostSuite/JovianBn256PairingInputOverLimitFails — 已运行通过 | core/vm/contracts.go:957, 1138, 1286, 1388；params/protocol_params.go:192-197 | 与 Isthmus 值逐项对应，FISCO 全表一致 |

## S-PDE — Predeploys

| 编号 | 规范要点（英文原文关键句+中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-PDE-1 | "Predeploy addresses exist in a prefixed namespace 0x4200…0xxx. Proxies are set at the first 2048 addresses … except GovernanceToken and WETH"（命名空间 0x4200…0xxx；前 2048 地址放代理，除 GovernanceToken/WETH） | predeploys.md:42-44 | MUST | 全分叉 | ✅ | 命名空间常量与冲突检查（build-allocs.py:152-153, 175-179）；全部 0x42… 代理来自 op-deployer 生成的 base allocs 且 expected_predeploys 断言带代码（build-allocs.py:598-605；chain-config-c2.yaml:23-43）；base allocs 脚本实测 0x…06 WETH/0x…42 GovToken 无代理、其余代理带 code | —（创世数据由 op-deployer 提供，非 op-geth 代码） | 创世数据=op-deployer 产物，FISCO 不合成 OP 账户（build-allocs.py:4-9） |
| S-PDE-2 | "The `LegacyERC20ETH` predeploy lives at a special address 0xDead…0000 and there is no proxy deployed at that account."（LegacyERC20ETH@0xDead…0000，无代理，仅余额） | predeploys.md:46-47 | MUST | 全分叉 | ✅ | base allocs 中 0xdead…0000 存在且 code 为空（脚本实测） | — | 创世注入 |
| S-PDE-3 | predeploy 全表：地址/引入分叉/废弃标记（Legacy/Bedrock/Canyon/Ecotone/Isthmus 共 20 项，"Deprecated contracts should not be used."） | predeploys.md:53-76 | MUST | 全分叉 | ✅ | base allocs 各地址均存在（脚本实测：0x…00, 02, 06, 07, 10-1a, 1b, 20, 21, 42, BeaconBlockRoot 全带 code/余额）；FISCO 创世即全部激活（无按分叉时间线部署） | — | 引入分叉（Legacy→Isthmus）在 FISCO 创世中一次性注入，无逐分叉激活路径（基线 Isthmus） |
| S-PDE-4 | "The `LegacyMessagePasser` … does not forward calls to the `L2ToL1MessagePasser` and calling it is considered a no-op"（LegacyMessagePasser 废弃、不转发） | predeploys.md:84-98 | MUST | Bedrock+ | ✅ | base allocs 含 0x…00（code=True，脚本实测）；行为在 op-deployer 字节码内 | — | 行为非 EL 代码；存在性由创世保证 |
| S-PDE-5 | "The `L2ToL1MessagePasser` stores commitments … proof … is in the `sentMessages` mapping … Any withdrawn ETH … removed … by calling the `burn()` function."（passer 存 sentMessages 承诺；ETH 可经 burn 移出 L2 供应） | predeploys.md:100-112 | MUST | Bedrock+ | ✅ | 同 S-WDL-1/S-WDL-7（地址注入 + 存储根计算）；burn 为合约方法（字节码内） | 同 S-WDL-7 | EL 侧存储根路径已由 S-WDL-7 覆盖 |
| S-PDE-6 | "`WETH9` is the standard implementation of Wrapped Ether … placed as a predeploy so that it is at a deterministic address"（WETH9@0x…06，无代理） | predeploys.md:149-157 | MUST | 全分叉 | ✅ | base allocs 0x…06 code=True（脚本实测）；chain-config.yaml:61-63 模板（vendored WETH.sol） | — | 创世注入 |
| S-PDE-7 | "The L1Block was introduced in Bedrock and is responsible for maintaining L1 context in L2."（L1Block@0x…15 维护 L1 上下文） | predeploys.md:260-269 | MUST | Bedrock+ | ✅ | OP_L1_BLOCK 常量（OpPredeploys.h:11）；L1 attributes deposit 目标校验 to==L1Block && from==depositor（OpBlockExecute.h:91-94）；deposit 写 L1Block 槽位测试 OpL1BlockDepositSuite/L1BlockDepositWritesSlots；EL 从 L1Block slot1/3/7/8 读费参数（OpFeeParams.cpp:25-38） | — | L1 上下文写入由 deposit 交易驱动（l1-attributes 章节） |
| S-PDE-8 | "The `GasPriceOracle` … only exists to preserve the API for offchain gas estimation. The function `getL1Fee(bytes)` … pre-Ecotone: scalar/overhead/decimals; post-Ecotone: baseFeeScalar/blobBaseFeeScalar, decimals=6, overhead ignored"（GasPriceOracle@0x…0F 保留 API；费参数语义按 Ecotone 切换） | predeploys.md:218-258 | MUST | Bedrock+ | ✅ | OP_GAS_PRICE_ORACLE 常量（OpPredeploys.h:12-13）；合约由 base allocs 提供；EL 侧 L1 fee 计算：Ecotone 公式（不含 overhead）has_ecotone_l1_formula（OpForkSchedule.cpp:16, 31）+ computeL1Cost 双公式（RollupCost.cpp:166-204, 含 calldataGas/16e6 与 Fjord 公式） | —（op-geth 侧为 rollup_cost.go 同公式） | decimals=6 与 overhead 忽略语义在 EL 费计算中体现（Ecotone 分支早于基线，属防御性保留） |
| S-PDE-9 | "The `SequencerFeeVault` … accumulates any transaction priority fee and is the value of block.coinbase. The `BaseFeeVault` predeploy receives the base fees … The `L1FeeVault` predeploy receives the L1 portion of the transaction fees."（三个 vault 分别收 priority/base/L1 费） | predeploys.md:280-334 | MUST | Bedrock+ | ✅ | EL 入账：priority fee→coinbase（SequencerFeeVault 0x…11 作 coinbase，OpTransition.cpp:315）；base fee→OP_BASE_FEE_VAULT（OpTransition.cpp:327）；L1 fee→OP_L1_FEE_VAULT（OpTransition.cpp:329）；地址常量 OpPredeploys.h:14-18；测试 OpZeroDiffTest.cpp:87-90（BaseFeeVault 差异注释）+ OpFeeParamsTest | params/protocol_params.go:25-30（OptimismBaseFeeRecipient/L1FeeRecipient） | 创世含三个 vault 带 code（脚本实测） |
| S-PDE-10 | "The `BeaconBlockRoot` predeploy provides access to the L1 beacon block roots … specified in EIP-4788"（BeaconBlockRoot@0x000F3df6…Beac02，EIP-4788） | predeploys.md:353-358 | MUST | Ecotone+ | ✅ | base allocs 该地址 code=True（脚本实测）；块首系统调用 system_call_block_start 在 CANCUN+ rev 执行 4788（OpBlockExecute.cpp:74-75；rev=EVMC_CANCUN/PRAGUE，OpForkSchedule.cpp:10, 24, 62, 76） | — | 4788 系统调用由 evmone 提供 |
| S-PDE-11 | "Governance Token … 0x4200…42"（GovernanceToken@0x…42） | predeploys.md:360-364 | MUST | 全分叉 | ✅ | base allocs 0x…42 存在（无 code、余额由 allocs 提供，脚本实测） | — | 创世注入 |
| S-PDE-12 | "OperatorFeeVault … Its address will be 0x4200…001b. This vault implements FeeVault, like BaseFeeVault, SequencerFeeVault, and L1FeeVault."（OperatorFeeVault@0x…1B，Isthmus 引入，同 FeeVault 模式） | predeploys.md:366-372；isthmus/predeploys.md:37-44 | MUST | Isthmus+ | ✅ | OP_OPERATOR_FEE_VAULT 常量（OpPredeploys.h:19-20）；EL 计费并入该 vault（OpTransition.cpp:330-333）；base allocs 0x…1b code=True（脚本实测）；测试 OpPredeploysTest.cpp:19（地址断言）+ AddressesMatchOpStackNamespace — 已运行通过 | params/protocol_params.go:34（OptimismOperatorFeeRecipient） | — |
| S-PDE-13 | Fjord："three additional values … costIntercept, costFastlzCoef, minTransactionSize … hard-coded constants … A new method is introduced: getL1FeeUpperBound"（Fjord 起 GPO 硬编码三常量 + getL1FeeUpperBound 上界公式） | fjord/predeploys.md:14-49 | MUST | Fjord+ | ✅ | 合约（vendored GasPriceOracle）由 base allocs 提供；EL 侧同套常量用于 L1 费：kL1CostIntercept=-42585600 / kL1CostFastlzCoef=836500 / kMinTxSizeScaled=1e8（RollupCost.cpp:14-17），estimatedDaSizeScaled 取 max 下限（RollupCost.cpp:126-131），Fjord 公式 scaled*(l1BaseFee*16*baseScalar+blobBaseFee*blobScalar)/1e12（RollupCost.cpp:203-207） | op-geth rollup_cost.go（Fjord L1 cost 同公式） | getL1FeeUpperBound 本身是合约方法（EL 无对应）；EL 侧 L1 费计算与合约常量一致 |
| S-PDE-14 | Fjord："The `getL1GasUsed` method is updated to take into account the improved compression estimation … LibZip.flzCompress(_data)"（getL1GasUsed 改用 flz 压缩估算；该函数在后续升级将 revert） | fjord/predeploys.md:51-79 | MUST | Fjord+ | ✅ | EL 侧等价压缩估算 flzCompressLenImpl（FastLZ 端口，RollupCost.cpp:23-90）用于 L1 费计算（computeL1CostFromFlz，RollupCost.cpp:204-207） | op-geth FlzCompressLen 同实现 | 合约方法 revert 行为在字节码内 |
| S-PDE-15 | Isthmus："`setIsthmus` … MUST only be callable by the `DEPOSITOR_ACCOUNT` once. When it is called, it MUST call each getter for the network specific config and set the returndata into storage."（L1Block.setIsthmus 仅 DEPOSITOR_ACCOUNT 可调、仅一次，调用网络配置 getter 写入存储） | isthmus/predeploys.md:25-27 | MUST | Isthmus+ | ✅ | OP_DEPOSITOR=0xdead…0001 常量（OpPredeploys.h:22）；deposit 目标/来源校验 to==L1Block && from==OP_DEPOSITOR（OpBlockExecute.h:91-94）；deposit 分发执行链（OpBlockExecute.cpp:102-114） | op-geth：L1 属性 deposit 携带 setL1BlockValues 选择子触发 | once 语义由合约自身标志位 + op-node activation 区块调度保证（L1 属性 deposit 内容校验 FISCO 有意放宽，见 OpBlockExecute.h:233-244 注释，与 op-geth/op-reth 一致）；"call each getter" 为合约逻辑 |
| S-PDE-16 | Isthmus："`getOperatorFee(uint256)` … returns the operator fee … capped at `U256` max value."（GasPriceOracle.getOperatorFee 按公式返回 operator fee，封顶 U256max） | isthmus/predeploys.md:31-35 | MUST | Isthmus+ | ✅ | 合约由 base allocs 提供；EL 侧 operator fee 公式实现（Isthmus：gas*scalar/1e6+constant；Jovian：gas*scalar*100+constant）computeOperatorCost（RollupCost.cpp:208-226，×100 常量 :20），计费走 OpTransition.cpp:324, 403 | op-geth isthmus/jovian operator fee 同公式 | U256 封顶为合约逻辑；Jovian ×100 公式见 jovian/exec-engine.md（operator fee 章节交叉） |

## S-PIN — Preinstalls

| 编号 | 规范要点（英文原文关键句+中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-PIN-1 | "Preinstalled smart contracts exist on Optimism at predetermined addresses in the genesis state."（15 个预安装合约在固定地址注入创世：Safe/SafeL2/MultiSend/MultiSendCallOnly/SafeSingletonFactory/Multicall3/Create2Deployer/CreateX/ArachnidProxy/Permit2/EntryPoint×2/SenderCreator×2） | preinstalls.md:25-53 | MUST | 全分叉 | ✅ | op-fork-base-allocs.json 中 15 个地址全部带 code（脚本逐地址实测）；base allocs 经 build-allocs.py 全量并入 FISCO 创世（build-allocs.py:596-622 merged=base+overlay），同一 merged alloc 输出 op-reth oracle 用 geth 格式 JSON（build-allocs.py:652-679）；base 产物 SHA-256 钉死（build-allocs.py:207-231, 708） | — | 无逐地址 expected_predeploys 断言（仅 OP predeploy 有），依赖 op-deployer 产物完整性 + sha256 钉扎 |
| S-PIN-2 | "When Canyon activates, the contract code at 0x13b0D85C… is set to 0x6080…"（Canyon 激活时 Create2Deployer 代码替换为指定字节码） | preinstalls.md:159-160 | MUST | Canyon+ | ✅ | base allocs 中 0x13b0d85c… 代码前缀与规范字节码逐字节一致（脚本实测，code 长 1584B） | — | FISCO 创世即 Canyon 后语义（基线 Isthmus），无运行时替换路径；若未来引入预 Canyon 链需补替换逻辑 |
| S-PIN-3 | "The keccak256 of the CreateX bytecode is 0xbd8a7ea8…"（CreateX 字节码 keccak 恒定值） | preinstalls.md:172 | MUST | 全分叉 | ✅ | base allocs 中 CreateX 字节码实测 keccak256=0xbd8a7ea8cfca7b4e5f5041d7d4b17bc317c5ce42cfbc42066a00cf26b43eb53f（用 build-allocs.py keccak256 计算，与规范逐字节相等） | — | 实证校验通过 |
| S-PIN-4 | "these contracts do not have the same security guarantees as Predeployed smart contracts"（预安装合约无预部署合约的安全保证） | preinstalls.md:31-34 | SHOULD | 全分叉 | ➖ | — | — | 信息性说明，无 EL 行为要求 |

## S-SYC — System Config（仅 EL 相关）

| 编号 | 规范要点（英文原文关键句+中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-SYC-1 | "The `SystemConfig` is a contract on L1 that can emit rollup configuration changes as log events. The rollup block derivation process picks up on these log events and applies the changes."（SystemConfig 为 L1 合约，经 ConfigUpdate 事件驱动配置变更） | system-config.md:53-58 | MUST | 全分叉 | ➖ | — | — | op-node 职责（从 L1 日志派生，经 Engine API PayloadAttributes 下发给 EL） |
| S-SYC-2 | "The V0 Batcher Hash is … version(0x00) ‖ address"; "batch inbox"; "Unsafe Block Signer … stored at keccak256(\"systemconfig.unsafeblocksigner\")"（batcher hash/batch inbox/unsafe block signer 槽位） | system-config.md:62-147 | MUST | 全分叉 | ➖ | — | — | op-node 职责（签名/批处理策略；unsafe signer 非共识参数） |
| S-SYC-3 | "The **L2 Gas Limit** defines the maximum amount of gas … The gas limit may not be set to a value larger than the maximum gas limit"（L2 gas limit 不得超最大上限；变更在引入该 L1 origin 的首个区块全量生效） | system-config.md:149-160 | MUST | 全分叉 | ✅ | EL 校验：payload.gasLimit > 2^63-1 拒绝（EngineServiceImpl.cpp:462-466，镜像 op-geth params.MaxGasLimit）；构建侧优先取 PayloadAttributes.gasLimit（op-node 传入的 SystemConfig 派生值，EngineServiceImpl.h:713-715），无 1/1024 调整（全量生效语义） | params/protocol_params.go:39（MaxGasLimit=0x7fffffffffffffff）；consensus/beacon/consensus.go:262-264 | gasUsed≤gasLimit 由执行侧两层保证（EngineServiceImpl.cpp:467-479 注释）；minimum/maximumGasLimit 函数本体为 L1 合约（➖） |
| S-SYC-4 | "System config updates are signaled through the `ConfigUpdate(uint256,uint8,bytes)` event … Type 0-7 … If a System Config Update cannot be parsed for any reason, it is not applied and is instead skipped."（ConfigUpdate 事件 type 0-7 编码；解析失败跳过） | system-config.md:174-197 | MUST | 全分叉 | ➖ | — | — | L1 合约（emit）+ op-node（解析/跳过）职责 |
| S-SYC-5 | "`setFeatureEnabled` … MUST only be triggerable by the ProxyAdmin or its owner. MUST toggle the feature flag … `isFeatureEnabled` Returns true if the feature is enabled"（Customizable Feature 开关函数） | system-config.md:227-241 | MUST | op-contracts v4.1.0+ | ➖ | — | — | L1 SystemConfig 合约职责；EL 不实现 toggle。FISCO 对应机制见 S-SYC-6（创世 feature_flags，无运行时 toggle——架构差异） |
| S-SYC-6 | "A **Customizable Feature** is a component of the OP Stack … behind some sort of toggle"（可定制特性由 feature flag 驱动，EL 按启用状态运行） | system-config.md:162-170 | MUST | 全分叉 | ✅ | FISCO 特性开关机制：feature_op_jovian（Features.h:125-127）自创世 [features] 读取，feature_flags 位图 = Features::toFlagsNumber()（Features.h:218）；fork 语义选择 configAt：jovianActive→Jovian，否则 Isthmus（OpForkSchedule.cpp:103-110）；SystemConfig predeploy（0x43…00C0）的 feature_flags Entry 槽由 build-allocs.py 写入（build-allocs.py:550-557, 344-362）并由 C++ 创世路径校验与节点特性集一致（Ledger.cpp:1826-1835, 1900-1963；测试 test_GenesisFeatureFlagsCommit.cpp） | — | OP 标准 EL 不读 feature flags（该机制为 FISCO 自定义，对应 OP 的 fork 时间戳激活）；与规范"运行时 toggle"不等价：FISCO 为创世固定、不可链上更新 |
| S-SYC-7 | "`minimumGasLimit` Returns the minimum L2 gas limit … sum of the maximum resource limit and the system transaction maximum gas. `maximumGasLimit` Returns the maximum …"（min/max gas limit 计算） | system-config.md:243-250 | MUST | 全分叉 | ➖ | — | — | L1 SystemConfig 合约函数；EL 侧仅消费最终 gasLimit（S-SYC-3） |
| S-SYC-8 | "`setEIP1559Params` … MUST only be callable by the chain governor … `_denominator` and `_elasticity` MUST be set to values greater to than 0"（Holocene：动态 EIP-1559 参数，denom/elasticity>0，编码进 header extraData 并用于 base fee 计算） | holocene/system-config.md:21-68 | MUST | Holocene+（FISCO 基线 Isthmus 内建） | ✅ | 构建侧：从 PayloadAttributes.eip1559Params（8B，SystemConfig 派生）解码 denom/elas 写入 header extraData 9B（version 0 ‖ u32 denom ‖ u32 elas）（EngineServiceImpl.h:723-748）；校验侧：extraData 恰 9B、version 0x00、denom/elas 非零（EngineServiceImpl.cpp:497-535）；应用侧：calcOpBaseFee 从 parent extraData 读 denom/elas 参与 EIP-1559 公式（EngineServiceImpl.cpp:293-312, 342-368）；测试 EngineProtoAlignB1Test（eip1559Params 解析）+ state_verify.py:81-99（extraData 解码 oracle） | consensus/misc/eip1559/eip1559_optimism.go:12-56（Validate/Encode/Decode 同布局）；miner/worker.go:380-394 | 事件编码（SystemConfig event）≠extraData 编码（holocene/system-config.md:32-33），EL 只用 extraData/属性形式；默认值 2/8（EngineServiceImpl.cpp:300-301 与 op-geth 默认一致） |
| S-SYC-9 | Isthmus："Isthmus adds configuration variables `operatorFeeScalar` (`uint32`) and `operatorFeeConstant` (`uint64`) … initialized to 0 … `setOperatorFeeScalars` MUST only be callable by the `SystemConfig` owner … ConfigUpdate type 5"（operator fee 参数配置/读取） | isthmus/system-config.md:21-35, 49, 76-84 | MUST | Isthmus+ | ✅ | 配置本体为 L1 SystemConfig 合约（➖ 部分）；EL 应用侧：从 L1Block slot8 读 operator_fee_scalar/constant（OpFeeParams.cpp:29-34, 33-34），computeOperatorCost 计费（RollupCost.cpp:208-226；OpTransition.cpp:324, 330-333, 403），has_operator_fee 开关（OpForkSchedule.cpp:65, 81）；测试 OpFeeParamsTest（slot8 解包） | —（op-geth 同从 L1Block 读取） | 默认 0 语义：slot8 缺失时读 0（OpFeeParams.cpp:24 注释 MissingAllSlotsZero 测试） |
| S-SYC-10 | Jovian："Jovian adds a configuration value `minBaseFee` (`uint64`, default 0) … Implementations MUST incorporate the configured value into the block header `extraData` [9,17) … `version` MUST be `1` … There MUST NOT be any data beyond these 17 bytes … if the computed `baseFee` is less than `minBaseFee`, it MUST be clamped"（minBaseFee 配置须编码进 header extraData[9,17) 且 base fee 下钳制） | jovian/system-config.md:23-68；jovian/exec-engine.md:32-54 | MUST | Jovian+ | 🟡 | 读取/校验/应用侧完整：calcOpBaseFee 读 extraData[9:17) 并 clamp（EngineServiceImpl.cpp:316-328, 370-374）；校验 17B/version 0x01/无多余字节（EngineServiceImpl.cpp:497-517）；**但构建侧硬编码尾 8 字节为 0**：Jovian 分支 extra.resize(17, 0x00)（EngineServiceImpl.h:749-750），payloadAttributes.minBaseFee 虽经 RPC 解析（EngineHelper.cpp:358；Types.h:106；PayloadId.h:222-224）却从未被引擎消费（grep 全 engine/ 无使用点）——非零 minBaseFee 无法上链，链上恒为规范默认 0 | miner/worker.go:380-394（Jovian 起 minBaseFee 必填，缺失报 "missing minBaseFee"）；consensus/misc/eip1559/eip1559_optimism.go:54（EncodeJovianExtraData(denom, elas, *minBaseFee)）、:12（version=0x01）、:157（DecodeJovianExtraData） | 偏差点：属性解析→PayloadId→构建的链路断在 extraData 编码；影响：minBaseFee 配置功能不可用（恒 0 与规范默认一致，故链上自洽，但无激活路径）；同时缺失 op-geth 的 post-Jovian minBaseFee 必填校验 |
| S-SYC-11 | Jovian："`daFootprintGasScalar` (`uint16`) … The `daFootprintGasScalar` is loaded … read from the deposited L1 attributes … or read from the L1 Block Info contract … direct storage-read: big-endian `uint16` in slot `8` at offset `12`"（DA footprint 标量配置与读取；blobGasUsed 改存 DA footprint） | jovian/system-config.md:82-116；jovian/exec-engine.md:121-143 | MUST | Jovian+ | ✅ | 主读取路径：deposit calldata[176:178]（激活区块 176B→0）（OpBlockExecute.cpp:124-135；OpBlockExecute.h:270-279）；等价 slot8 读法（OpFeeParams.cpp:33）；应用：seal.blobGasUsed=Σda_footprint（OpBlockExecute.cpp:319-331；OpCommitments.h:103-105 比较），base fee 用 max(gasUsed,blobGasUsed)（EngineServiceImpl.cpp:332-339）；has_da_footprint 开关（OpForkSchedule.cpp:82）；测试 OpL1BlockDepositSuite（JovianActivationDepositOnlySeals 等）+ OpDaMatrixSchema 全通过 | —（op-geth 同经 L1 attributes/存储读标量） | 读取源（deposit calldata vs 存储）等价；ConfigUpdate type 7 事件为 L1 合约+op-node 职责（➖ 部分） |
| S-SYC-12 | Jovian："The `minBaseFee` MUST be `null` prior to the Jovian fork, and MUST be non-`null` after the Jovian fork."（PayloadAttributesV3.minBaseFee 在 Jovian 前后必为 null/非 null） | jovian/exec-engine.md:59-79 | MUST | Jovian+ | 🟡 | RPC 解析支持可选字段（EngineHelper.cpp:358；EngineProtoAlignB1Test.cpp:90-145 验证 null/非 null 往返）；但无"Jovian 后必须非 null"的强制校验（与 op-geth miner/worker.go:381-382 的 "missing minBaseFee" 报错对照缺失） | miner/worker.go:380-382 | 与 S-SYC-10 同根因（构建侧不消费该属性）；影响：属性语义缺失，但链上行为恒等于默认 0 |

## 汇总

- 条款总数：45（S-WDL 8、S-PRE 5、S-PDE 16、S-PIN 4、S-SYC 12）
- 判定计数：✅ 30；🟡 2；❌ 0；➖ 13
- 重点差距（🟡）：
  1. S-SYC-10：Jovian minBaseFee 构建侧硬编码 0——extraData 尾 8 字节恒零（EngineServiceImpl.h:749-750），PayloadAttributes.minBaseFee 解析后未消费；影响：非零 minBaseFee 配置无法生效（链上恒为默认 0，自洽但无激活路径）。
  2. S-SYC-12：PayloadAttributesV3.minBaseFee 缺 post-Jovian 必填校验（op-geth 有 "missing minBaseFee" 硬报错）。
- 存疑点：
  - S-WDL-7/S-PDE-5：L2ToL1MessagePasser 实际字节码为 op-deployer 产物（base allocs），本审计只验证注入与 EL 存储根计算，未审计合约源码语义（burn/sentMessages 哈希格式）。
  - S-PDE-15：L1Block.setIsthmus 的"仅调用一次/激活区块调用"由合约字节码与 op-node 调度保证，EL 仅保证 deposit 目标/来源（FISCO 有意不校验 L1 属性 deposit 内容，与 op-geth/op-reth 一致）。
  - S-PIN-1：预安装合约无 expected_predeploys 式逐地址断言，完整性依赖 op-deployer 产物 + base_allocs_sha256 钉扎。
  - FISCO 以 Isthmus 为基线（feature_op_jovian 选 Jovian），Canyon/Holocene 语义内建于创世/基线，无独立分叉激活路径——凡"适用分叉"早于 Isthmus 的条款均按创世已激活处理。


## 模块：m3-engine-api

# M3+M4 审计：Engine API 校验面（S-EXE）+ 区块构建面（S-BLD）

- 模块：OP Stack 执行客户端（EL）spec 对齐审计 — 子任务 M3（Engine API 方法/校验/错误码）+ M4（区块构建面）
- 负责章节：`/tmp/op-specs/specs/protocol/exec-engine.md` 全章；`regolith/`、`canyon/`、`ecotone/`、`fjord/`、`granite/`、`holocene/`、`isthmus/`、`jovian/` 各目录 exec-engine.md 中 Engine API/校验/构建增量条款
- 规范基线：ethereum-optimism/specs @ commit `2049036afe878a7cb443f513f4e6ca453d90c340`
- 分叉基线：Isthmus 默认 + Jovian（feature_op_jovian）；Karst 不归本模块
- FISCO 取证：worktree `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment`（分支 feat-opstack-e2e，含未提交改动，按文件当前内容取证）
- 参照实现：op-geth @ commit `d0734fd5f44234cde3b0a7c4beb1256fc6feedef`（/Users/octopus/octo/code/op-geth）
- 术语：W=worktree 根；G=op-geth 根；S=/tmp/op-specs
- 行号均经 grep/读取验证；FISCO 路径省略 W 前缀、op-geth 路径省略 G 前缀、spec 路径省略 S 前缀

## 判定统计

| 判定 | 数量 |
| --- | --- |
| ✅ 符合 | 39 |
| 🟡 部分符合 | 3 |
| ❌ 缺失 | 1 |
| ➖ 不适用 | 3 |
| 合计 | 46 |

## S-EXE 条款（Engine API 方法面 / 校验面 / 错误码）

| 编号 | 规范要点（原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| S-EXE-1 | `engine_forkchoiceUpdatedV2` 扩展 PayloadAttributesV2：新增 `transactions`（array of DATA）、`noTxPool`（bool）、`gasLimit`（QUANTITY or null）；"The `gasLimit` is optional w.r.t. compatibility with L1, but required when used as rollup" | specs/protocol/exec-engine.md:223-267 | MUST | Bedrock~Canyon（结构面全分叉） | ✅ | PayloadAttributes 含 transactions/noTxPool/gasLimit 字段：bcos-framework/bcos-framework/engine/Types.h:90-106；JSON 解析：bcos-rpc/bcos-rpc/web3jsonrpc/utils/EngineHelper.cpp:293-337（transactions 逐项 hex 校验:300-309，noTxPool 严格 bool:311-321，gasLimit 严格 uint64:326-337）；FCU 端点接线：bcos-rpc/bcos-rpc/web3jsonrpc/endpoints/EngineEndpoint.cpp:73-98 | eth/catalyst/api.go:147-162（FCU V2）；BuildPayloadArgs 字段 miner/payload_building.go:52-62 | 结构面 ✅；"rollup 必填 gasLimit"的拒绝语义见 S-EXE-5 |
| S-EXE-2 | FCU 更新 canonical 链语义：headBlockHash 标记 unsafe、safe/finalized 分别对应 L1 派生边界；未知 head → SYNCING | specs/protocol/exec-engine.md:210-219 | MUST | 全分叉 | ✅ | updateForkchoice 持久化 m_forkchoiceState 与 tracked head：engine/bcos-engine/EngineServiceImpl.h:367-372；任一哈希未入库 → SYNCING:294-303；safe/finalized ≤ head 次序校验:304-321（映射问题见 S-EXE-19）；head 回退/序号跳跃 → 拒绝:338-364 | eth/catalyst/api.go:223-240（未知 head 拉取/返回 SYNCING） | head/安全边界深度语义（consolidation/reorg）归 S-DRV 审计 |
| S-EXE-3 | "If present and non-empty [transactions]: the payload MUST be produced starting with this exact list of transactions"；执行出错必须返回 STATUS_INVALID | specs/protocol/exec-engine.md:247-263 | MUST | 全分叉（OP 构建面） | ✅ | 强制交易列表置于首位：EngineServiceImpl.h:648-654（buildOpPayload）与 :1642-1651（buildPayload）；执行失败分类：OpConsensusRejected→INVALID 且 latestValidHash=已验父:581-594（mapDelegateError） | checkOptimismPayloadAttributes eth/catalyst/api_optimism.go:40-65；tx 强制列表入 generateParams txs:miner/payload_building.go:302-304 | E2E 测试覆盖 INVALID/-32603 分类：opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp:1221-1224 |
| S-EXE-4 | `noTxPool=true`："the execution engine must not change anything about the given list of transactions"（禁止从交易池打包） | specs/protocol/exec-engine.md:254-258 | MUST | 全分叉（OP 构建面） | ✅ | noTxPool=true 时跳过 mempool remove/seal：EngineServiceImpl.h:623-628（buildOpPayload）、:430-434（buildPayload） | BuildPayloadArgs.NoTxPool miner/payload_building.go:60；worker 侧 noTxPool 逻辑 | 单节点/无 CL 场景由 synthesizeL1AttributesEnvelope 补首笔 deposit（:643-647，仅 attrs 无交易时） |
| S-EXE-5 | "The `gasLimit` ... required when used as rollup. If not specified as rollup, a `STATUS_INVALID` is returned" — rollup 模式 FCU 带 attrs 但缺 gasLimit 必须返回 INVALID | specs/protocol/exec-engine.md:265-267 | MUST | 全分叉（rollup 模式） | 🟡 | FCU 不拒绝缺省 gasLimit：buildOpPayload 回退 ledgerConfig（EngineServiceImpl.h:714-716）；OP 面跳过 validatePayloadAttributes 预检（`if constexpr (!c_opMode)`，:270-283）；另 eip1559Params 非法尺寸/缺失也被中性 1/1 吞掉（:727-739） | checkOptimismPayloadAttributes：`if payloadAttributes.GasLimit == nil { return errors.New("gasLimit parameter is required") }` eth/catalyst/api_optimism.go:41-43，FCU 返回 STATUS_INVALID + InvalidPayloadAttributes（-38003）:eth/catalyst/api.go:215-218 | 偏差影响：CL 误发缺 gasLimit 的 attrs 时 FISCO 用本地 ledger 配置构建而非拒绝 — 单节点自洽但违反规范拒绝语义；与 op-geth 行为不等价 |
| S-EXE-6 | `engine_forkchoiceUpdatedV3` "**must only be called with Ecotone payload**"；PayloadAttributesV3 扩展 parentBeaconBlockRoot（32B） | specs/protocol/exec-engine.md:271-299 | MUST | Ecotone+ | ✅ | validatePayloadAttributes：V3 要求 parentBeaconBlockRoot（EngineServiceImpl.cpp:225-228）、V1/V2 拒绝:213-224；FCU V3/V4 端点接线 EngineEndpoint.cpp:85-98 | FCU V3 gate eth/catalyst/api.go:164-178（withdrawals/beaconRoot 必填） | OP 面恒 Isthmus+，V3 请求仅在带 attrs 时被接受（EngineServiceImpl.h:392-398） |
| S-EXE-7 | "Starting at Ecotone, the `parentBeaconBlockRoot` must be set to the L1 origin `parentBeaconBlockRoot`, or a zero `bytes32` if the Dencun functionality ... is not active on L1" | specs/protocol/exec-engine.md:301-302 | MUST | Ecotone+ | ✅ | buildOpPayload：parentBeaconBlockRoot = attrs 值或零哈希（EngineServiceImpl.h:703-704），且透传到 newPayload 校验（validateOpNewPayloadRequest:409-412 必填）与 getPayload 回显（:843） | 构建入 header.BeaconRoot：beacon/engine/types.go:440（envelope ParentBeaconBlockRoot: block.BeaconRoot()） | 值本身由 op-node 下发，FISCO 不做 L1 侧推导（EL 职责外） |
| S-EXE-8 | "Starting with Holocene, the `eip1559Params` field must encode the EIP1559 parameters. It must be `null` before"；Holocene+ attrs 中 eip1559Params 必须恰好 8 字节（u32 BE denominator + u32 BE elasticity） | specs/protocol/exec-engine.md:304-305；holocene/exec-engine.md:74-82 | MUST | Holocene+ | ✅ | JSON 解析层强制 8 字节：EngineHelper.cpp:338-357（尺寸不符 → InvalidParams -32602）；字段入 payload ID：bcos-framework/bcos-framework/engine/PayloadId.h:218-221 | ValidateHolocene1559Params（8 字节 + 0/0 同时性）consensus/misc/eip1559/eip1559_optimism.go:117-128；FCU 校验 api_optimism.go:54-60 | "Holocene 前必须 null"在 FISCO 无 pre-Holocene 时间戳面（恒 Isthmus+），不适用；FCU 时非法参数拒绝见 S-EXE-5 |
| S-EXE-9 | "Starting with Jovian, the `minBaseFee` field is added. It must be `null` before Jovian" | specs/protocol/exec-engine.md:307-308；jovian/exec-engine.md:79 | MUST | Jovian+ | ✅ | 解析面：EngineHelper.cpp:358-369（严格 uint64）；入 payload ID：PayloadId.h:222-232 | op-node 侧字段；op-geth BuildPayloadArgs.MinBaseFee miner/payload_building.go:62 | Jovian 前 null 语义无时间戳面不适用；构建消费见 S-BLD-4（❌） |
| S-EXE-10 | `engine_newPayloadV2` "No modifications"；`engine_newPayloadV3` "**must only be called with Ecotone payload**"；`engine_newPayloadV4` "**must only be called with Isthmus payload**" | specs/protocol/exec-engine.md:310-331 | MUST | 全分叉 | ✅ | 版本门：OP 面 handleOpNewPayload 仅收 V4，否则 UnsupportedFork（EngineServiceImpl.h:1132-1138）；端点 4 版本全注册：EndpointsMapping.cpp:68-71、EngineEndpoint.cpp:167-205 | NewPayloadV3/V4 时间戳 fork 门 eth/catalyst/api.go:598-660 | 见 S-EXE-19：UnsupportedFork 异常在 RPC 层未映射为 -38005 |
| S-EXE-11 | `engine_newPayloadV3` "expectedBlobVersionedHashes MUST be an empty array"；`engine_newPayloadV4` "executionRequests MUST be an empty array" | specs/protocol/exec-engine.md:321-335；isthmus/exec-engine.md:206 | MUST | Ecotone+ / Isthmus+ | ✅ | expectedBlobVersionedHashes 非空拒绝：EngineServiceImpl.cpp:405-408；executionRequests 解析恒 nullopt（EngineHelper.cpp:199-204）且非空拒绝：EngineServiceImpl.cpp:571-574 | 规范条款直接对应；op-geth 侧 executionRequests 必填（api.go:626-628） | executionRequests 字段本身（Types.h:179）OP 面永为空列表，对应 isthmus "Block Sealing 无 EIP-6110/7002/7251"（isthmus/exec-engine.md:186-194，执行面归其他 agent） |
| S-EXE-12 | Isthmus 后 `ExecutionPayload` 含额外字段 `withdrawalsRoot`；`engine_newPayloadV4` 使用之；header 有效性规则：withdrawalsRoot 必须 32 字节且等于 L2ToL1MessagePasser 账户 storage root | specs/protocol/exec-engine.md:328-335；isthmus/exec-engine.md:57-78,198-206 | MUST | Isthmus+ | ✅ | 解析 V4-only 且 V1-V3 忽略：EngineHelper.cpp:189-197；OP 面必填：EngineServiceImpl.cpp:413-418；重建入 header：rebuildOpEthHeader :609；构建回填执行结果 :800；与执行侧六向比较固定（含 withdrawalsRoot）：opstack-executor/OpCommitments.h:41-49（比较 :92-93）、OpSchedulerSeam.h:66-69 | checkOptimismPayload：post-Isthmus 必须非 nil、pre-Isthmus 必须 nil eth/catalyst/api_optimism.go:26-33 | FISCO OP 面恒 Isthmus+，"pre-Isthmus 必须 nil"分支不适用 |
| S-EXE-13 | `engine_getPayloadV2` "No modifications"；V3/V4 "must only be called with Ecotone/Isthmus payload"；版本兼容由 payloadId 首字节版本决定 | specs/protocol/exec-engine.md:337-370 | MUST | 全分叉 | ✅ | getPayload 版本兼容：isGetPayloadVersionCompatible（EngineServiceImpl.cpp:143-165，V2→≤2、V3→≤3、V4→≤4）；未知 payload → UnknownPayload 异常（EngineServiceImpl.h:894-904）；RPC 面 EngineError::UnknownPayload 枚举 bcos-rpc/bcos-rpc/web3jsonrpc/utils/Common.h:33-42 | getPayload 按 payloadID 版本 + timestamp fork 校验：eth/catalyst/api.go:446-465（V4 请求接受 PayloadV3 载荷） | FISCO 版本兼容面略宽于 op-geth（≤N vs 精确版本），自产载荷下等价；UnknownPayload 的 -38001 映射见 S-EXE-19 |
| S-EXE-14 | `engine_getPayloadV3` 响应扩展：executionPayload / blockValue / blobsBundle / shouldOverrideBuilder / parentBeaconBlockRoot；"In Ecotone it MUST be set to the parentBeaconBlockRoot from the L1 Origin block" | specs/protocol/exec-engine.md:348-364 | MUST | Ecotone+ | ✅ | combineGetPayloadResponse 全字段：EngineHelper.cpp:506-564（blobsBundle 恒空数组:524-551，executionRequests 空数组 V4:560-563）；parentBeaconBlockRoot 由 buildOpPayload 恒设置（value_or 零哈希，EngineServiceImpl.h:703-704,843）并序列化 :553-556 | envelope 组装 beacon/engine/types.go:440（omitempty） | FISCO 仅在 has_value 时输出该字段；OP 构建路径恒有值，行为一致 |
| S-EXE-15 | `engine_exchangeCapabilities` 能力协商：CL 与 EL 取公共最高版本 | specs/protocol/exec-engine.md:206（Engine API 通用面） | MUST | 全分叉 | ✅ | supportedOpCapabilities 13 项（10 基础 + V4 三件套）：EngineServiceImpl.cpp:121-141；exchangeCapabilities 经 if constexpr 分支：EngineServiceImpl.h:234-250；RPC 端点注册 EndpointsMapping.cpp:59 | 反射枚举全部 engine_ 方法：eth/catalyst/api.go:934-946 | V4 三件套与 OP 面 V4-only 门一致（EngineServiceImpl.cpp:131-140 注释） |
| S-EXE-16 | `engine_signalSuperchainV1`：可选扩展（"Optional extension to the Engine API"）；SHOULD 警告 recommended 更新、SHOULD 对 required 采取措施（可 halt） | specs/protocol/exec-engine.md:372-401 | SHOULD（可选） | 全分叉 | ➖ | 未实现（EndpointsMapping 无注册；能力列表无该条目） | SignalSuperchainV1 eth/catalyst/superchain.go:21-39（含 HandleRequiredProtocolVersion 停机逻辑:33-36） | 规范明示可选扩展，不实现不违反；注 op-geth 已实现 |
| S-EXE-17 | 错误码表：-38001 UnknownPayload、-38002 InvalidForkchoiceState、-38003 InvalidPayloadAttributes、-38005 UnsupportedFork、-38006 TooDeepReorg（execution-apis 错误码语义） | specs/protocol/exec-engine.md:206（引用 execution-apis） | MUST | 全分叉 | 🟡 | 枚举定义齐全：bcos-rpc/bcos-rpc/web3jsonrpc/utils/Common.h:33-42；但 engine 异常→JSON-RPC 码映射**未接线**：UnsupportedFork/UnknownPayload/InvalidForkchoiceState 等 bcos 异常经 Web3JsonRpcImpl `catch (bcos::Error const&)` → 一律 -32603（bcos-rpc/bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.cpp:91-103；bcos::Error 派生自 bcos::Exception：bcos-utilities/bcos-utilities/Error.h:48）；代码内自述"not implemented yet"：EngineServiceImpl.h:78-84 | op-geth 错误类型映射 engine.InvalidPayloadAttributes/UnsupportedFork/UnknownPayload 至对应码：eth/catalyst/api.go:447,462；beacon/engine/types.go:161-180 | 影响：真实 op-node 收到 -32603 而非 -38005/-38001/-38002，CL 侧无法按错误码分支；EngineEndpoint.cpp:112-116 的 TODO 注明同类缺失 |
| S-EXE-18 | newPayload 校验分类：blockHash 不符/静态校验失败 → INVALID 且 latestValidHash=null（parentKnown 之前）；parent 未知 → SYNCING；执行失败 → INVALID latestValidHash=parent | specs/protocol/exec-engine.md:310-335（execution-apis 状态机语义） | MUST | 全分叉 | ✅ | 静态校验 INVALID+null：EngineServiceImpl.h:1212-1235；parentKnown 查询走 SYS_HASH_2_NUMBER → SYNCING:1289-1295；parent 已验后 latestValidHash=parent:1300；E2E 覆盖四分类与 latestValidHash 取值的组合完备性断言：opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp:1221-1230 | invalid() latestValid=parent/nil：eth/catalyst/api.go:860-869；SYNCING:787-793；known-block VALID:772-777 | 已覆盖任务要求的 INVALID/SYNCING/-38005/-32603 面 |
| S-EXE-19 | 已知块重交付 → 直接 VALID 短路（不重执行） | specs/protocol/exec-engine.md:310（execution-apis 语义） | MUST | 全分叉 | ✅ | runOpNewPayloadSteps step 3b：EngineServiceImpl.h:1425-1430（SYS_HASH_2_NUMBER 命中即 VALID，放置在静态校验之后） | "Ignoring already known beacon payload" eth/catalyst/api.go:772-777 | 放置次序（静态校验后、non-tip 检查前）有注释论证:1416-1420 |
| S-EXE-20 | blockHash 重算校验：payload.blockHash 必须等于按 21 字段 header 计算的 keccak(RLP) | specs/protocol/exec-engine.md:310（execution-apis 语义） | MUST | 全分叉 | ✅ | computeHash vs payload.blockHash：EngineServiceImpl.h:1231-1235；header 重建 rebuildOpEthHeader：EngineServiceImpl.cpp:578-620（3 个 post-merge 常量 applyOpHeaderConstants:100-106；requestsHash 恒 OP 空值:c_opEmptyRequestsHash:96-97） | op-geth 不做显式 blockHash 比对（block 由字段重建、blockHash 仅查缓存） | FISCO 更严格：错误 blockHash 显式 INVALID（不会波及其他）；测试证据 "blockHash does not match the reconstructed block header"：OpNewPayloadRpcE2eTest.cpp:1242 |
| S-EXE-21 | extraData：Holocene 后每个块 header extraData 必须恰为 9 字节（version=0 ‖ u32 BE denominator ‖ u32 BE elasticity），denominator/elasticity 非零、无额外数据；Jovian 扩为 17 字节（version=1 ‖ 同 8 字节 ‖ u64 BE minBaseFee，minBaseFee 任意） | specs/protocol/exec-engine.md:57-62；holocene/exec-engine.md:35-53；jovian/exec-engine.md:38-57 | MUST | Holocene+ / Jovian+ | ✅ | 形状+版本字节+非零校验：validateOpNewPayloadRequest EngineServiceImpl.cpp:496-537（Isthmus 9B/0x00:509-518、Jovian 17B/0x01:498-507、denom/elasticity 非零:529-536）；E2E 向量："extraData must be exactly 9 bytes..." / "17 bytes..."：OpNewPayloadRpcE2eTest.cpp:1243-1244 | ValidateHoloceneExtraData / ValidateJovianExtraData consensus/misc/eip1559/eip1559_optimism.go:147-204；经 verifyHeader（beacon/consensus.go:238-243）与 newPayload（api_optimism.go:22-24）双路径 | Holocene 前块 extraData 必须为空的条款（exec-engine.md:57-58）在 FISCO OP 面（恒 Isthmus+）不适用 |
| S-EXE-22 | 新块构建与 newPayload 校验自洽：buildOpPayload 产出必须通过自身的静态校验与执行六向比较（extraData/baseFeePerGas/blobGasUsed/gasUsed/stateRoot/receiptsRoot/withdrawalsRoot 一致） | specs/protocol/exec-engine.md:206-370（自洽性） | MUST | Isthmus+/Jovian+ | ✅ | 两遍执行：probe（verify=false，占位承诺）→ 回填真实承诺（:793-809）→ canonical（verify=true，六向比较:822-826）；blockHash 由最终 header computeHash:810-812；自建 payload 走 runOpNewPayloadSteps 快路径提交:1245-1272 | op-geth 构建-提交环：BuildPayload → newPayload（InsertBlockWithoutSetHead） | 构建侧 gasLimit/blobGasUsed 与校验侧规则同源（calcOpBaseFee、DA footprint 检查），无双轨漂移 |
| S-EXE-23 | baseFee 一致性校验：newPayload 中 payload.baseFeePerGas 必须等于按父块（Holocene 起读父 extraData 参数）重算的值 | specs/protocol/exec-engine.md:310；holocene/exec-engine.md:101-111 | MUST | Holocene+ | ✅ | step 3a-2：EngineServiceImpl.h:1385-1400（calcOpBaseFee(parent) ≠ payload.baseFeePerGas → INVALID latestValidHash=parent） | VerifyEIP1559Header→CalcBaseFee consensus/misc/eip1559/eip1559.go:30-46 | 与 S-BLD-5 同源实现，校验/构建一致 |
| S-EXE-24 | 1559 参数配置面：EL 须支持 per-chain EIP1559 denominator/elasticity；Canyon 后新 denominator（EIP1559DenominatorCanyon）；Holocene 起参数由父块 extraData 动态决定 | specs/protocol/exec-engine.md:45-53；holocene/exec-engine.md:103-111 | MUST | 全分叉 | ✅ | calcOpBaseFee 默认 8/2 并从父 extraData 读 denom/elasticity（EngineServiceImpl.cpp:299-312）；buildOpPayload 编码版本字节区分 Holocene/Jovian（EngineServiceImpl.h:740-751） | CalcBaseFee：`DecodeOptimismExtraData(config, parent.Time, parent.Extra)` consensus/misc/eip1559/eip1559.go:73-77 | FISCO 无 Canyon 独立分母分支（恒 Holocene+ 走 extraData），Isthmus+ 链上等价；Canyon 常量面（exec-engine.md:46-47）不适用 |
| S-EXE-25 | Jovian baseFee 更新使用 `gasMetered := max(gasUsed, blobGasUsed)`（DA footprint 参与 baseFee 上涨） | jovian/exec-engine.md:128-130 | MUST | Jovian+ | ✅ | calcOpBaseFee：parentIsJovian 时 gasMetered=max(gasUsed, blobGasUsed)（EngineServiceImpl.cpp:336-340）；注释引用 op-geth eip1559.go:99-107 | calcBaseFeeInner：`if *parent.BlobGasUsed > parent.GasUsed { parentGasMetered = *parent.BlobGasUsed }` consensus/misc/eip1559/eip1559.go:98-106 | 与 op-geth 逐行等价 |
| S-EXE-26 | 非 Holocene 块（genesis 除外）extraData 必须为空 | specs/protocol/exec-engine.md:57-58 | MUST | pre-Holocene | ➖ | OP 面恒 Isthmus+，无 pre-Holocene 区块面 | ValidateOptimismExtraData：pre-Holocene 非空拒绝 eip1559_optimism.go:27-29 | 分叉基线不含 pre-Holocene，不适用 |
| S-EXE-27 | Ecotone 禁用 blob 交易（EIP-4844）：txpool 拒收、块构建不选、状态转换含 blob 交易无效；BLOBBASEFEE 恒压 1 | specs/protocol/exec-engine.md:473-490 | MUST | Ecotone+ | ✅ | 交易载体级拒绝：validateRawTransactionKind 拒绝 Blob/Unsupported 类型（EngineServiceImpl.cpp:173-187），attrs（:207）与 payload（:242）双路径；OP 面 excessBlobGas 必须 0（:419-422）、pre-Jovian blobGasUsed 必须 0（:427-435） | OP 无 blob：op-geth 不选 blob tx（miner 侧无 blob 池）；blobGasUsed 语义见 S-BLD-7 | BLOBBASEFEE opcode 恒 1 属 EVM 执行面（opstack-executor/OpBlockExecute），不归本模块 |
| S-EXE-28 | 交易池不可消费 deposit 交易（deposits 仅经 Engine API / 可信哈希同步进入） | specs/protocol/exec-engine.md:82-87 | MUST | 全分叉 | ✅ | OP 面 rawTransactions 为唯一交易载体（EngineServiceImpl.cpp:394-400 要求必填）；deposit（0x7E）经 attrs.transactions 强制列表进入（EngineServiceImpl.h:648-654），mempool seal 仅产出 Web3 交易（:655-669 类型过滤） | 交易池禁止 deposit 类 | 池侧 0x7E 拒收证据在 txpool 层（不归本模块），engine 侧不混用两通道 |
| S-EXE-29 | JWT 鉴权（Engine API 强制，HS256 + 共享密钥文件 + iat 时差容忍） | specs/protocol/exec-engine.md:206（引用 execution-apis authentication） | MUST | 全分叉 | ✅ | op_engine_rpc 独立端口 + JWT 校验器：RpcFactory.cpp:441-456（HS256 白名单:446）；HTTP 入口强制校验：Web3JsonRpcImpl.cpp:190-225（失败 401/403 + -32010/-32011：bcos-rpc/bcos-rpc/jwtAuth/JwtErrors.h:55-72、jsonrpc/Common.h:77-78）；HS256+iat+clockSkew 实现：jwtAuth/JwtVerifier.cpp:71-101；默认端口 8551：bcos-tool/bcos-tool/NodeConfig.cpp:847 | geth JWT 中间件（eth/rpc，同 execution-apis 规范） | HTTP 状态码映射（401/403）与 execution-apis 要求一致 |
| S-EXE-30 | 端点分域：Engine API 与 web3 接口分端口提供，Engine 端口仅 HTTP、禁 WS/CORS | specs/protocol/exec-engine.md:206（Engine API 网络面） | MUST | 全分叉 | ✅ | initWeb3RpcServiceConfig：op_engine_rpc 独立 listen/port、CORS 关闭、禁 WS（RpcFactory.cpp:336-349）；Engine 方法仅 enableOPEngine 时注册（EndpointsMapping.cpp:41-54,56-73）；双端口实例构建 RpcFactory.cpp:533-541 | op-geth Engine API 仅 authrpc/HTTP | |
| S-EXE-31 | `engine_getClientVersion`（execution-apis 方法，OP spec 未引用） | —（OP spec 无条款） | — | 全分叉 | ➖ | 未实现（EndpointsMapping 无注册、能力列表无） | GetClientVersionV1 eth/catalyst/api.go:948-963 | OP spec exec-engine.md 不含此方法，不实现不违反；op-geth 有实现 |
| S-EXE-32 | FCU 带 attrs 时错误路径：attrs 无效 → 返回 INVALID/-38003 而非中止 forkchoice 更新（"the spec requires that fcu is applied when called on a valid hash, even if params are wrong"） | specs/protocol/exec-engine.md:265-267（隐式）；op-geth 注释 api.go:172-176 | MUST | 全分叉 | 🟡 | FISCO 先应用 forkchoice 更新、后处理 attrs（EngineServiceImpl.h:367-408 顺序正确）；但 OP 面不做 attrs 深度校验（见 S-EXE-5），withdrawals 非空/缺 parentBeaconBlockRoot 被静默规整（构建面强制空 withdrawals:757、零哈希 root:703-704），而非 INVALID/-38003 | op-geth：checkOptimismPayloadAttributes 先行，错误 → STATUS_INVALID + InvalidPayloadAttributes（api.go:215-218） | 偏差影响：CL 发非法 attrs（非空 withdrawals、缺 beacon root）时 FISCO 静默纠正继续构建，验证方将判块 INVALID — 错误被延迟而非拒绝 |
| S-EXE-33 | Deposit 交易类型支持：EIP-2718 抽象下 0x7E deposit 由引擎处理（mint/EVML1 info） | specs/protocol/exec-engine.md:64-73 | MUST | 全分叉 | ✅ | 0x7E 由 Web3Transaction RLP 解码支持（EngineServiceImpl.cpp:34-71 注释 0x04/0x7E）；强制列表透传 EIP-2718 原文（EngineServiceImpl.h:650-654） | op-geth DepositTx 类型 | deposit 语义执行面归其他 agent（opstack-executor） |

## S-BLD 条款（区块构建面：attributes 消费）

| 编号 | 规范要点（原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| S-BLD-1 | FCU+attrs → 启动区块构建并返回 payloadId（确定性派生，含 eip1559Params/minBaseFee 等 attrs 字段） | specs/protocol/exec-engine.md:265-267（构建触发）；holocene/exec-engine.md:84-87（"If `eip1559Params != null`, the `eip1559Params` is included in the `PayloadID` hasher directly after the `gasLimit` field"） | MUST | 全分叉（OP） | ✅ | buildOpPayload 触发构建并缓存（EngineServiceImpl.h:603-878）；payloadId 派生：derivePayloadId → PayloadId.h:136-243（sha256 顺序 parentHash‖timestamp‖prevRandao‖feeRecipient‖RLP(withdrawals)‖beaconRoot‖[noTxPool‖txCount‖txHashes]‖gasLimit‖eip1559Params‖minBaseFee，首字节版本） | BuildPayloadArgs.Id() miner/payload_building.go:66-102（同序；SlotNum 为 Amsterdam 扩展，OP 不发送） | payloadId 派生语义细节（sequence/时序）归 S-DRV 审计；此处只录 attrs 字段入哈希事实 |
| S-BLD-2 | attrs.gasLimit 覆盖构建 gasLimit（"This field overrides the gas limit used during block-building"） | specs/protocol/exec-engine.md:266 | MUST | 全分叉（OP） | ✅ | .gasLimit = attrs.gasLimit 优先，缺失回退 ledgerConfig（EngineServiceImpl.h:714-716） | miner/payload_building.go:302-303（gasLimit: args.GasLimit）；worker 应用 :394 | 回退行为见 S-EXE-5（规范要求缺失即 INVALID，FISCO 回退） |
| S-BLD-3 | attrs.eip1559Params → 新块 header extraData 编码（u32 BE denominator + u32 BE elasticity；version 0=Holocene） | specs/protocol/exec-engine.md:304；holocene/exec-engine.md:96-99 | MUST | Holocene+ | ✅ | extraData lambda 解码 8 字节 attrs 并编码 9 字节（EngineServiceImpl.h:725-753） | EncodeOptimismExtraData/EncodeHoloceneExtraData consensus/misc/eip1559/eip1559_optimism.go:49-60,102-111；worker.go:394 | 偏差点：attrs 缺失/尺寸≠8 时 FISCO 用中性 1/1（:727-739）而 op-geth FCU 直接拒绝（S-EXE-5）— 自洽但非法输入被吞；"unless both are 0 → prior constants"（holocene:98-99）语义由 op-node 侧保证，FISCO 不重建该分支 |
| S-BLD-4 | Jovian attrs.minBaseFee → 新块 extraData [9,17)（u64 BE wei），version=1；baseFee 计算时低于 minBaseFee 必须钳制 | jovian/exec-engine.md:40-54,79 | MUST | Jovian+ | ❌ | **构建侧不消费 minBaseFee**：extraData lambda 对 Jovian 仅 `extra[0]=0x01; extra.resize(17,0x00)`（EngineServiceImpl.h:747-751）— minBaseFee 尾部恒零；attrs.minBaseFee 仅被解析（EngineHelper.cpp:358-369）与入 payloadId（PayloadId.h:222-232），未写入 extraData | EncodeJovianExtraData：`binary.BigEndian.PutUint64(r[9:], minBaseFee)`；`if minBaseFee == nil { panic(...) }` consensus/misc/eip1559/eip1559_optimism.go:49-54,180-190；worker.go:394 | 影响：SystemConfig 下发 minBaseFee>0 时（op-node attrs.minBaseFee），FISCO 构建块 extraData 与参照实现不同 → blockHash 不同 → 外部验证方判 INVALID；链上 baseFee 下限（calcOpBaseFee:370-374 从父块读）随之失效。单节点自洽（自产自验）但跨客户端不一致 |
| S-BLD-5 | 构建侧 baseFeePerGas 按父块重算（Holocene 参数 + Jovian max(gasUsed,blobGasUsed) + Jovian minBaseFee 钳制），不得沿用父块 baseFee | specs/protocol/exec-engine.md:45-53；holocene/exec-engine.md:103-111；jovian/exec-engine.md:47-54,128-130 | MUST | Holocene+/Jovian+ | ✅ | buildOpPayload：从 SYS_NUMBER_2_BLOCK_HEADER 读父头 → calcOpBaseFee（EngineServiceImpl.h:678-694）；calcOpBaseFee 完整实现（denom/elasticity 8/2 默认、Jovian gasMetered、minBaseFee floor）：EngineServiceImpl.cpp:293-377 | CalcBaseFee consensus/misc/eip1559/eip1559.go:64-89（含 minBaseFee 钳制:85-89） | 与 op-geth 算法逐项对应；无父块时初始 1e9（=params.InitialBaseFee，EngineServiceImpl.h:680）与 op-geth :68-71 一致 |
| S-BLD-6 | Jovian 构建侧：header blobGasUsed = 块 DA footprint（Σ 非 deposit 交易的 daUsageEstimate×daFootprintGasScalar）；构建/校验保证 daFootprint ≤ gasLimit | jovian/exec-engine.md:98-126 | MUST | Jovian+ | ✅ | 构建从执行头回填 blobGasUsed（EngineServiceImpl.h:806-809，注释：预置 0 会挂六向比较）；newPayload 静态校验 blobGasUsed≤gasLimit（EngineServiceImpl.cpp:557-560） | CalcDAFootprint core/types/rollup_cost.go:559+；校验 core/block_validator.go:119-132（footprint≠blobGasUsed 或 >gasLimit 均拒） | DA footprint 计算本体（OpBlockSeal/rollup_cost）属执行面（OpBlockExecute.h），本模块录"入 blobGasUsed + 限额校验"的构建/校验两侧 |
| S-BLD-7 | pre-Jovian（Isthmus 及更早）header blobGasUsed 必须 0（OP 无 blob） | specs/protocol/exec-engine.md:473-490（禁用 blob）；jovian/exec-engine.md:121-122（pre-Jovian 恒 0） | MUST | pre-Jovian | ✅ | validateOpNewPayloadRequest：非 Jovian 时 blobGasUsed≠0 拒绝（EngineServiceImpl.cpp:427-435）；构建预置 0（EngineServiceImpl.h:758） | op-geth OP 无 blob 语义 | |
| S-BLD-8 | 构建侧 withdrawals 列表：OP 恒空（Canyon+ 空 RLP 列表；Isthmus 后 body 仍为空） | isthmus/exec-engine.md:161-163；canyon 语义（exec-engine.md 引上海） | MUST | Canyon+ | ✅ | buildOpPayload 恒空列表（EngineServiceImpl.h:757）；newPayload 要求 present 且 empty（EngineServiceImpl.cpp:401-404）；header withdrawalsRoot 独立承载 MessagePasser storage root（:609） | OP 空 withdrawals 语义 | 非空 withdrawals 的拒绝语义见 S-EXE-32（FCU attrs 面静默规整） |
| S-BLD-9 | 强制交易列表（attrs.transactions）必须按序置于块首并参与 transactionsRoot | specs/protocol/exec-engine.md:247-252 | MUST | 全分叉（OP） | ✅ | envelopes 组装：合成 deposit（仅无 attrs 交易时）→ attrs.transactions 原文 → sealed mempool（EngineServiceImpl.h:637-669）；transactionsRoot 由 raw 列表计算（:767 computeTxRoot；执行侧同源 OpBlockExecute） | op-geth：args.Transactions 入 generateParams.txs（miner/payload_building.go:302-304） | 合成 deposit 仅限无 CL 的单节点 fixture（:640-647 注释），op-node 场景不双写 |
| S-BLD-10 | noTxPool=true 时构建不得含 mempool 交易（块内仅强制列表） | specs/protocol/exec-engine.md:254-258 | MUST | 全分叉（OP） | ✅ | EngineServiceImpl.h:623-628 | BuildPayloadArgs.NoTxPool miner/payload_building.go:60 | |
| S-BLD-11 | 构建面 requestsHash：Isthmus 后恒 sha256('')（EIP-7685 no-op，无 deposit/consolidation requests） | isthmus/exec-engine.md:77-78,184-194 | MUST | Isthmus+ | ✅ | c_opEmptyRequestsHash 常量并写入重建 header（EngineServiceImpl.cpp:96-97,615）；executionRequests 非空拒绝（:571-574）；getPayloadV4 响应恒空 executionRequests（EngineHelper.cpp:560-563） | op-geth 空 requests | 请求生成（block sealing 不调 EIP-6110/7002/7251 系统调用）属执行面 |
| S-BLD-12 | 构建面 header 常数：ommersHash=keccak(rlp([]))、difficulty=0、nonce=0（post-merge OP 常量） | specs/protocol/exec-engine.md:310（execution-apis header 语义） | MUST | 全分叉 | ✅ | applyOpHeaderConstants（EngineServiceImpl.cpp:100-106）与常量（:93-97）；getPayload 序列化由 header 重建路径一致产出 | op-geth 常量（beacon/consensus.go:225-231 校验） | |
| S-BLD-13 | 构建-校验闭环：构建产出的 payload 必须能通过自身 newPayload 全部静态校验与执行六向比较 | specs/protocol/exec-engine.md:260-263（"must execute the transactions in order and return STATUS_INVALID if there is an error"） | MUST | 全分叉（OP） | ✅ | 两遍执行自证（S-EXE-22）+ 自建快路径提交（EngineServiceImpl.h:1245-1272）；构建 gasLimit 上限 2^63-1 与校验同源（EngineServiceImpl.cpp:462-466） | op-geth 构建后 newPayload 环 | 任务重点核对项："新块构建与 newPayload 校验自洽性" — 结论自洽（同一 calcOpBaseFee/形状校验/六向比较双端） |

## 重点差距清单（按影响排序）

1. **S-BLD-4（❌）**：buildOpPayload 不把 attrs.minBaseFee 写入 Jovian extraData [9,17)（EngineServiceImpl.h:747-751 尾部恒零）。SystemConfig 下发 minBaseFee>0 时构建的块与 op-geth 不一致 → 跨客户端 blockHash 不一致被判 INVALID；链上 minBaseFee 下限失效。
2. **S-EXE-17（🟡）**：engine 异常（UnsupportedFork/-38005、UnknownPayload/-38001、InvalidForkchoiceState/-38002）未映射为对应 JSON-RPC 错误码，RPC 层一律 -32603（Web3JsonRpcImpl.cpp:91-103）。op-node 无法按错误码区分 -38005（fork 不匹配）与内部错误。
3. **S-EXE-5 / S-EXE-32（🟡）**：FCU 带 attrs 时无 gasLimit 必填校验（回退 ledgerConfig 而非 INVALID）；eip1559Params 尺寸非法、withdrawals 非空、缺 parentBeaconBlockRoot 被静默规整而非 -38003/INVALID — op-geth 在 FCU 时即拒绝（api_optimism.go:40-65）。
4. **S-EXE-17 备注（🟡）**：UnknownPayload 在 EngineEndpoint.cpp:158 的 JsonRpcException(-38001) 为死代码 — service 层先抛 bcos 异常，-38001 实际不可达。
5. **S-BLD-3（🟡 边界）**：attrs.eip1559Params 缺失/尺寸≠8 时构建侧中性 1/1 兜底，op-geth FCU 拒绝；仅影响非法输入场景。

## 存疑点

- EngineServiceImpl.h:1140-1152 的 "independent review #5429 finding B" 注释称 OP 组合根 maxEngineVersion 无法提至 V4、V4 端点未注册 — 与当前代码不符（libinitializer/Initializer.cpp:620 已传 V4；EndpointsMapping.cpp:63/67/71 已注册 V4 三件套；supportedOpCapabilities 已含 V4）。注释过期，未影响代码行为，但易误导后续维护。
- FCU V3（带 attrs）构建出的 payload 以 version=3 缓存，而 newPayload 仅收 V4：若 op-node 协商出 V3 后以 newPayloadV3 回送自建块，将被 UnsupportedFork 拒绝（EngineServiceImpl.h:1132-1138）。FISCO 注释声称 op-node 对 Isthmus+ 用 FCU V3 建块 + newPayloadV4 提交，链上自洽；但若 op-node 按 exchangeCapabilities 协商为 V3 全链调用，则存在构建/提交版本不对称风险（需 op-node 侧版本选择验证，属 S-DRV 范围）。
- calcOpBaseFee 的 minBaseFee floor 依赖父块 extraData 尾 8 字节（EngineServiceImpl.cpp:314-327）：一旦 S-BLD-4 修复（构建写 minBaseFee），读取侧已就绪，二者是一对。
- Jovian DA footprint 上限校验采用 `blobGasUsed > gasLimit`（EngineServiceImpl.cpp:557）与 op-geth `daFootprint > GasLimit()`（block_validator.go:131）一致（等于允许）；DA footprint 计算值本身由执行侧 OpBlockSeal 产出并经六向比较固定，本模块未重算。


## 模块：m5-derivation

# M5 派生边界（derivation.md EL 侧）+ P0 动态验证 —— OP Stack EL 规范对齐审计

- 审计日期：2026-08-21
- 规范基线：/tmp/op-specs（commit 2049036）
- FISCO 代码：worktree op-alignment（分支 feat-opstack-e2e，含未提交改动，按当前文件内容取证）
- 参照实现：op-geth 本地源码，commit `d0734fd5f44234cde3b0a7c4beb1256fc6feedef`（optimism 分支）
- 分叉基线：Isthmus 默认 + Jovian（feature_op_jovian）。Karst 不归本子任务。
- 取证文件：
  - FISCO：`engine/bcos-engine/EngineServiceImpl.h`、`engine/bcos-engine/EngineServiceImpl.cpp`、`bcos-framework/bcos-framework/engine/PayloadId.h`、`bcos-framework/test/unittests/engine/TestPayloadId.cpp`
  - op-geth：`eth/catalyst/api.go`、`miner/payload_building.go`、`consensus/misc/eip1559/eip1559.go`、`consensus/beacon/consensus.go`
  - op-reth（P0 参照）：v1.2.0 `crates/optimism/payload/src/{builder,payload,...}.rs`（main 分支已无 crates/optimism）

## 条款表

| 编号 | 规范要点（英文原文关键句+一句中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-DRV-1 | "block.timestamp = prev_l2_timestamp + l2_block_time"；EL 侧体现为 newPayload 必须拒绝时间戳不严格递增的 payload（块间 l2_block_time 等差由 op-node batch queue 保证） | /tmp/op-specs/specs/protocol/derivation.md:132-137, 141-154 | MUST | 全部 | ✅ | EngineServiceImpl.h:1314-1383（Step 3a：读父头比较 `payload.timestamp <= parentHeader->timestamp()` 即 INVALID+latestValidHash=parent；ms 内部单位与父头同单位） | eth/catalyst/api.go:745-747（`block.Time() <= parent.Time()` → INVALID）；consensus/beacon/consensus.go:253-256（errInvalidTimestamp） | EL 只保证单调性；`+l2_block_time` 等差是 op-node 派生职责。FISCO 检查先于已知块短路（注释论证观察等价，:1329-1331） |
| S-DRV-2 | newPayload 时父块未知 → 返回 SYNCING（"parent missing … return SYNCING"），供 op-node 驱动自身同步 | 语义源自 exec-engine.md（Engine API 方法面归 Engine API agent）；此处核派生边界行为 | MUST | 全部 | ✅ | EngineServiceImpl.h:1284-1295：走存储 `SYS_HASH_2_NUMBER` 查 parentHash，无值即 `makeStatus(Syncing, null, null)`（注释明确"op-node relies on SYNCING to drive its own sync"） | eth/catalyst/api.go:740-743（parent==nil → `delayPayloadImport` → SYNCING，:787-804） | 与 op-geth 区别：FISCO 用存储查询（非内存 map），语义一致。错误码细分面归 Engine API agent |
| S-DRV-3 | 已入链的 payload 重投递必须直接回 VALID，不得重复执行（CL 超时重发、op-node 重启重放 unsafe 块是常规路径） | 规范未逐字规定，行为依据 op-geth + derivation.md:846-855 的 getPayload/newPayload 流程幂等性 | SHOULD | 全部 | ✅ | EngineServiceImpl.h:1403-1430（Step 3b：`SYS_HASH_2_NUMBER` 命中 blockHash → VALID；注释说明重执行会因 nonce 已推进/双倍 mint 而误判 INVALID） | eth/catalyst/api.go:733-736（known block → VALID，先于其它校验） | 顺序差异已注释论证（先 2/3a/3c 后 3b vs op-geth 最先 3b），观察等价 |
| S-DRV-4 | "the function that computes the next block's base fee from its parent block header"——newPayload 必须按父块头重算 baseFeePerGas 并校验 payload（Holocene+ 动态 EIP-1559：参数取自父头 extraData；Jovian 加 minBaseFee 下限与 DA footprint 取 max） | /tmp/op-specs/specs/protocol/holocene/exec-engine.md:95-118；exec-engine.md:43-49 | MUST | Holocene+（Isthmus/Jovian） | ✅ | EngineServiceImpl.cpp:293-377（calcOpBaseFee：弹性/分母自父头 extraData、Jovian minBaseFee、blobGasUsed 替代 gasUsed）；EngineServiceImpl.h:1385-1400（Step 3a-2 与 payload.baseFeePerGas 比对，不符 INVALID+latestValidHash=parent） | consensus/misc/eip1559/eip1559.go:33-53（VerifyEIP1559Header→CalcBaseFee），:64（CalcBaseFee），Jovian 扩展 :86-107 | FISCO 注释逐条对照 op-geth eip1559.go（:1386-1390, cpp:314-340, 370-374） |
| S-DRV-5 | "Engine: Get Payload to retrieve the payload, by the payload-ID in the result of the previous step"——payloadId 必须是 (parentHash, attributes) 的确定性函数，字节级对齐参照实现，保证 getPayload 可寻址 | /tmp/op-specs/specs/protocol/derivation.md:851-852 | MUST | 全部 | ✅ | PayloadId.h:136-243（derivePayloadId：sha256(parent‖ts_sec BE‖prevRandao‖feeRecipient‖RLP(withdrawals)‖[beaconRoot]‖[noTxPool‖txCount‖keccak(tx)*]‖[gasLimit]‖[eip1559Params]‖[minBaseFee]) 取前 8B，byte0=version；ts ms→s 于 :149）；EngineServiceImpl.h:440, 611（调用点）；TestPayloadId.cpp:67-81（golden "0x010f846a7ea7b1aa" 对齐 op-geth） | miner/payload_building.go:66-101（BuildPayloadArgs.Id()） | op-reth v1.2.0 payload.rs:308-355（payload_id_optimism）同构。FISCO 无 SlotNum（flashblocks）字段，与 op-geth SlotNum==nil 字节一致（PayloadId.h:118-124 自注）。注：代码中无"encodePayloadSequence"命名，即此 derivePayloadId |
| S-DRV-6 | "The payload attributes are chosen in favor of the previous unsafe L2 block, creating an L2 chain reorg on top of the current safe block. Immediately processing the new alternative attributes enables execution engines like go-ethereum to enact the change, as linear rewinds of the tip of the chain may not be supported"——consolidation 失败时 EL 必须能接受 parent 非当前 tip 的 payload（linear rewind / reorg） | /tmp/op-specs/specs/protocol/derivation.md:824-827；另见 :875-879（unsafe payload parent 须匹配当前 unsafe head，说明替换链会以非 tip parent 到达 EL） | MUST | 全部 | ❌ | EngineServiceImpl.h:1432-1468（Step 3c：子高度已被占即抛 OpExecutionInternalError(-32603)，注释自认"non-tip parent not supported … architecture work, parked"） | eth/catalyst/api.go:278-293（FCU SetCanonical 支持回退/reorg；OP 分支不忽略旧 head），:760-767（InsertBlockWithoutSetHead） | M5 最重大差距：L1 reorg / consolidation 失败 / safe-head 之上重建时，op-node 发送的替代 attributes 父块不是当前 tip，FISCO 无法执行而持续回 -32603，节点卡死无法恢复。规范原文明确点名"enables execution engines like go-ethereum"即要求 EL 具备该能力 |
| S-DRV-7 | Pipeline reset 语义：从现有 L2 链回退足够深度以对齐当前 L1（"Resetting will recover the pipeline … starting from an existing L2 chain that is traversed back just enough to reconcile with the current L1 chain"）——OP 模式下 FCU 必须能接受 head 指回非当前 tip 的更新（reorg 触发点） | /tmp/op-specs/specs/protocol/derivation.md:898-914 | MUST | 全部 | ❌ | EngineServiceImpl.h:337-349（`headBlockNumber < trackedHeadBlock.blockNumber` 时返回 VALID 但**不更新** forkchoice 状态，即静默忽略旧 head 更新） | eth/catalyst/api.go:278-293（OP 模式下 `SetCanonical` 将 canonical 头回退；仅非 OP 模式忽略旧 head，:291-293） | 与 S-DRV-6 同源（reorg 能力缺失的 FCU 面）。影响：L1 reorg 后 op-node 的 FCU 回退被静默吞掉，safe/unsafe 状态陈旧，派生无法对齐 |
| S-DRV-8 | parent/child 编号连续性：payload 块号必须是父块号+1（batch 格式的 parent_hash 字段要求 + EL 索引一致性） | /tmp/op-specs/specs/protocol/derivation.md:414-425（batch 含 parent_hash/epoch/timestamp）；EL 侧行为依据 op-geth | MUST | 全部 | ✅ | EngineServiceImpl.h:1308-1312（`payload.blockNumber != *parentBlockNumber + 1` → INVALID+latestValidHash=parent；防按号索引表被覆盖） | eth/catalyst/api.go:741（`GetBlock(block.ParentHash(), block.NumberU64()-1)`——编号不连续等价于父查询失败→SYNCING） | 行为差异：FISCO 显式 INVALID，op-geth 走父查询失败回 SYNCING；两者均拒绝错误块，FISCO 更严格且不触发无谓 sync |
| S-DRV-9 | Engine Queue 维护 finalized/safe/unsafe 三个 L2 head 引用（"The stage maintains references to three L2 blocks"），forkchoice 更新先于派生输入应用 | /tmp/op-specs/specs/protocol/derivation.md:711-724, 785-797 | MUST | 全部 | ➖ op-node 职责 | EL 侧仅接收 FCU 三 hash 并做有序校验（safe≤head、finalized≤head，EngineServiceImpl.h:287-321） | eth/catalyst/api.go:204+（forkchoiceUpdated 处理三 hash） | 分工依据：三头维护与同步调度在 op-node engine queue；方法面/错误码归 Engine API agent |
| S-DRV-10 | L1-consolidation payload attributes matching：按字段比对派生 attrs 与已有 unsafe 块（parent_hash/timestamp/randao/fee_recipient/transactions_list/gas_limit，Canyon+ 加 withdrawals，Ecotone+ 加 parent_beacon_block_root），相等则该块转 safe | /tmp/op-specs/specs/protocol/derivation.md:799-822 | MUST | 全部 | ➖ op-node 职责 | EL 侧对应面（getPayload 结果与 attrs 逐字段一致）即构建面，归 Engine API agent（FISCO buildOpPayload EngineServiceImpl.h:603-878 按 attrs 构建） | miner/payload_building.go（buildPayload） | 分工依据：比对逻辑在 op-node；EL 只保证 getPayload 返回与 attrs 一致 |
| S-DRV-11 | L1-sync 处理序列：FCU(带 attrs 起建)→getPayload(按 payloadId)→newPayload(导入)→FCU(无 attrs 定 canonical，safe/unsafe 均指向新块) | /tmp/op-specs/specs/protocol/derivation.md:846-855 | MUST | 全部 | ➖ op-node 职责 | — | eth/catalyst/api.go（对应方法面） | 方法序列/版本面归 Engine API agent |
| S-DRV-12 | 错误处理：payload 处理错误→丢弃 attrs、forkchoice 不变；仅含 deposit 的 attrs 仍无效→critical error；forkchoice 校验错误→reset pipeline | /tmp/op-specs/specs/protocol/derivation.md:857-863, 892-896 | MUST | 全部 | ➖ op-node 职责 | — | — | 分工依据：错误驱动逻辑在 op-node engine queue |
| S-DRV-13 | unsafe payload 处理：块号须高于 safe head（safe 仅能因 L1 reorg 被移出），parent 须匹配当前 unsafe head（防跳链/防 reorg 已验证块） | /tmp/op-specs/specs/protocol/derivation.md:867-879 | MUST | 全部 | ➖ op-node 职责 | EL 侧对应即 parentKnown→SYNCING（S-DRV-2 已核） | — | 分工依据：判定在 op-node；EL 侧行为已覆盖 |
| S-DRV-14 | Resetting the Pipeline：FindL2Heads（finalized 缺失→genesis、safe 缺失→finalized）、resetting stages、reset 后首任务为 forkchoice 更新 | /tmp/op-specs/specs/protocol/derivation.md:898-967 | MUST | 全部 | ➖ op-node 职责 | EL 侧 reset 相关（FCU 更新能力）已并入 S-DRV-7 核 | — | 分工依据：reset 算法全在 op-node |
| S-DRV-15 | 派生 payload attributes 的交易顺序：L1 attributes deposit → user deposits → network upgrade txs → sequenced txs（"Transactions must appear in this order in the payload attributes"） | /tmp/op-specs/specs/protocol/derivation.md:1012-1022 | MUST | 全部 | ➖ op-node 派生面 | FISCO buildOpPayload 保证 L1 attrs deposit 在前（EngineServiceImpl.h:643-654：无 attrs/空 attrs 时合成前置 deposit；有 attrs 时按 CL 顺序） | — | 交易构造/排序是 op-node 派生职责；EL 侧强制顺序执行归 execution agent |
| S-DRV-16 | 构建 payload attributes 字段：timestamp=batch 时间、random=L1 prev_randao、suggestedFeeRecipient=Sequencer Fee Vault、transactions、noTxPool=true、gasLimit=SystemConfig、withdrawals（Canyon 前 nil/后空数组） | /tmp/op-specs/specs/protocol/derivation.md:1041-1053 | MUST | 全部 | ➖ op-node 职责 | attrs→payload 的构建面归 Engine API agent（buildOpPayload 已按 attrs 填充：EngineServiceImpl.h:672-764） | — | 分工依据：attrs 构造在 op-node；构建面归 Engine API agent |
| S-DRV-17 | Isthmus 激活块：按序含 L1 attrs tx + user deposits + 8 笔网络升级交易（L1Block/GasPriceOracle/OperatorFeeVault 部署、三 Proxy 更新、setIsthmus、EIP-2935 部署），noTxPool=true | /tmp/op-specs/specs/protocol/isthmus/derivation.md:21-39（逐笔细节 :40-343） | MUST | Isthmus | ➖ op-node 派生面 | EL 侧执行这些 deposit 交易（合约部署/升级）归 execution agent | — | 分工依据：升级交易由 derivation pipeline 确定性生成（op-node 侧）；FISCO 无派生管道 |
| S-DRV-18 | Isthmus span batch 增量：type-4（EIP-7702 SetCode）交易编码；含 type-4 的 singular batch 仅当 Isthmus 在该 batch 时间戳激活才接受，否则 DROP；按单个 batch 而非 span batch 判断 | /tmp/op-specs/specs/protocol/isthmus/derivation.md:345-381 | MUST | Isthmus | ➖ batch 解码/验证为 op-node | EL 侧 type-4 执行为 first-class（EngineServiceImpl.cpp:164-172 提及 0x04 经 Web3Transaction 解码；执行面归 execution agent） | — | 分工依据：span batch 解码/activation 检查在 op-node batch queue |
| S-DRV-19 | Jovian 激活块规则：不得含非 deposit 交易（sequencer 以 noTxPool=true 强制）；derivation 时 batch 含交易则 DROP；激活块按序含 L1 attrs tx + user deposits + 5 笔升级交易（L1Block 部署、L1Block Proxy 更新、GasPriceOracle 部署、GPO Proxy 更新、setJovian） | /tmp/op-specs/specs/protocol/jovian/derivation.md:17-39（逐笔 :41-251） | MUST | Jovian | ➖ op-node 派生面/batch queue | EL 侧 Jovian 头/baseFee 扩展已核（S-DRV-4）；DA footprint 上限校验归 Engine API agent（EngineServiceImpl.cpp:547-560 有实现） | — | 分工依据：激活块 noTxPool 与 DROP 判定在 op-node |

### 判定汇总

- 条款总数：19
- ✅ 符合：6（S-DRV-1, 2, 3, 4, 5, 8）
- ❌ 缺失：2（S-DRV-6, 7）
- 🟡 部分符合：0
- ➖ 不适用（op-node 职责）：11（S-DRV-9 … 19）

### 重点差距（需跟进）

1. **S-DRV-6（❌）non-tip parent / reorg 能力缺失**：consolidation 失败或 L1 reorg 时 op-node 会在 safe block 之上重建替代链，payload 的 parent 不是当前 tip；FISCO Step 3c（EngineServiceImpl.h:1432-1468）直接抛 -32603 拒绝，节点无法恢复。规范 derivation.md:824-827 明确要求 EL 具备该能力（"enables execution engines like go-ethereum to enact the change"），op-geth 以 InsertBlockWithoutSetHead + SetCanonical 支持。
2. **S-DRV-7（❌）FCU head 回退被静默忽略**：EngineServiceImpl.h:341-349 对 headBlockNumber < tracked 的 FCU 返回 VALID 但不更新状态；op-geth OP 分支会 SetCanonical 回退（api.go:278-293）。与 S-DRV-6 同源，L1 reorg 场景下 safe/unsafe 状态陈旧。

### 分工边界说明

- 方法面（API 版本/参数形状）、校验规则与错误码细分（-38005/-38003/SYNCING/ACCEPTED 枚举）、getPayload 返回构建面、Jovian DA footprint 校验：归 Engine API agent。
- execution 面（deposit 执行、type-4 执行、EIP-2935 合约部署语义）：归 execution agent。
- op-node 专属（derivation pipeline、batch/span-batch 解码、L1 派生输入、attrs 构造、consolidation 比对、pipeline reset、安全级别维护）：本表一律 ➖ 并注明分工依据。


## 模块：m6-rpc-genesis

# M6 审计：RPC 用户面 + 创世/predeploy 接线

- 模块：OP Stack EL（执行客户端）spec 对齐审计 — 子任务 M6
- 负责章节：deposits.md（回执元数据/API 输出）、exec-engine.md（receipts/block 结构 RPC 面）、isthmus/exec-engine.md 与 jovian/exec-engine.md（receipts 元数据）、withdrawals.md（Isthmus 后消息证明/eth_getProof）、system-config.md（feature_flags）、创世接线（op-deployer allocs、L1Block 注入）
- 规范基线：/tmp/op-specs @ commit 2049036afe878a7cb443f513f4e6ca453d90c340
- FISCO 代码：worktree op-alignment（分支 feat-opstack-e2e，含未提交改动，以文件当前内容为准）
- 参照实现：op-geth @ commit d0734fd5f44234cde3b0a7c4beb1256fc6feedef（本地源码 /Users/octopus/octo/code/op-geth）
- 分叉基线：Isthmus（默认）+ Jovian（feature_op_jovian）；Karst 不归本子任务

## 条款表

### S-RPC 组（用户 RPC 面）

| 编号 | 规范要点（英文原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-RPC-1 | "With Regolith: the `nonce` is set to the `depositNonce` attribute of the corresponding transaction receipt."（deposit tx 的 JSON nonce 必须取回执中的 depositNonce，而非恒 0） | deposits.md:83-85 | MUST | Regolith+（基线 Isthmus 恒激活） | ✅ | bcos-rpc/.../model/TransactionResponse.cpp:24-31（收到回执且 opStackMeta.deposit_nonce 存在时用 toQuantity(*meta->deposit_nonce) 覆盖 0x0）；无回执时保留 0x0（DepositTransaction.cpp:151），与 op-geth 无回执路径一致 | internal/ethapi/api.go:1208-1209（`if receipt != nil && receipt.DepositNonce != nil { result.Nonce = ... }`） | v1 疑点"nonce 硬编码 0x0"已修复；Regolith 前恒 0 分支基线不适用 |
| S-RPC-2 | "API responses contain zeroed signature `v`, `r`, `s` values ... Includes new `sourceHash`, `from`, `mint`, and `isSystemTx` attributes."（deposit tx JSON 必须输出 sourceHash/from/mint/isSystemTx 且 v/r/s 为零） | deposits.md:86-89 | MUST | 全部（Ecotone 0x7E 起） | ✅ | bcos-rpc/.../model/DepositTransaction.cpp:117-156（combineDepositTxResponse：type=0x7e、sourceHash、checksum from/to、gas/value/input、mint 仅 has_value 时、isSystemTx 仅 true 时、nonce/gasPrice/v/r/s=0x0） | internal/ethapi/api.go:1199-1207（SourceHash、IsSystemTx 仅 true、Mint 赋值）；core/types/deposit_tx.go:94-96（rawSignatureValues 全 0） | mint/isSystemTx 的 omitempty 语义与 op-geth 一致 |
| S-RPC-3 | "The RLP-encoded consensus-enforced fields are: ... `depositNonce` ... `depositNonceVersion` ... With Canyon, these ... must always be included."（depositNonce/depositReceiptVersion 必须进入共识回执；Canyon 后恒含、version=1） | deposits.md:217-228 | MUST | Regolith+/Canyon+（基线恒激活） | ✅ | bcos-evm/bcos-evm/opstack/OpTransition.cpp:577-580（runDeposit 设置 meta.deposit_nonce=preNonce、meta.deposit_receipt_version=1）；bcos-framework/.../protocol/TransactionReceipt.h:37-52,82-83（OpStackReceiptMeta 字段）；bcos-tars-protocol/.../protocol/TransactionReceiptImpl.cpp:142-145（calculateHash 覆盖含 opStackMeta 的 tars 全结构） | core/types/receipt.go:52（CanyonDepositReceiptVersion=1）、:74-80、:272、:359-360、:485-488（depositReceiptRLP 编码/解码） | 回执哈希域是 FISCO tars 序列化而非 EIP-2718 RLP，receiptsRoot 跨客户端一致性由 M5 的 receiptsRoot 计算保证；runDeposit 无条件写 version=1（无 Canyon 前省略分支，基线 Isthmus 无碍） |
| S-RPC-4 | "Starting with Regolith, the receipt API responses utilize ... The `depositNonce` is included in the receipt JSON ... For contract-deployments (when `to == null`), the `depositNonce` helps derive the correct `contractAddress`"（receipt JSON 输出 depositNonce；合约创建时 contractAddress 用执行前 nonce 推导） | deposits.md:230-235 | MUST | Regolith+ | ✅ | bcos-rpc/.../model/ReceiptResponse.cpp:126-129（输出 depositNonce/depositReceiptVersion，nullopt 时省略）；OpTransition.cpp:573（`toFiscoContractAddress(dep.from, preNonce)` 推导创建地址） | internal/ethapi/api.go:1796-1801（deposit 回执输出 depositNonce/depositReceiptVersion）；core/state_transition.go:342 起（合约地址推导） | 创建地址用执行前 nonce，与 spec 一致 |
| S-RPC-5 | "Deposited transactions MUST never be consumed from the transaction pool."（deposit 不得经交易池进入区块） | exec-engine.md:87 | MUST | 全部 | ✅ | bcos-rpc/.../endpoints/EthEndpoint.cpp:434-441,453-457（dispatchRawTransaction 分类 + deposit 双门拒绝）；mempool/bcos-mempool/MemPoolImpl.cpp:58-66（第二道门，in-process 调用者） | core/txpool/legacypool/legacypool.go:326-334（FilterType 仅收 Legacy/AccessList/Dynamic/SetCode，0x7E 落 default:false） | 拒绝发生在提交前，无状态影响 |
| S-RPC-6 | "The storage root should be the same root that is returned by `eth_getProof` at the given block number."（header 的 withdrawalsRoot 必须等于 eth_getProof 在该块返回的 MessagePasser 存储根；是 Isthmus 消息证明的用户面接口） | isthmus/exec-engine.md:58-59 | MUST | Isthmus+ | ❌ | bcos-rpc/.../endpoints/EthEndpoint.cpp:1001-1144（getProof 结构完整、storageHash 取自 stateRoot 的 MPT :1048-1084）；但 bcos-ledger/bcos-ledger/Ledger.cpp:2398（MPT 节点仅在 genesis 导入时写盘，运行时块不持久化）；tools/op-e2e/rpc_matrix.py:221-224（自认：非创世块 getProof 返回 -32602/-32004 "Block stateRoot not in MPT node storage"，钉为已知范围边界） | internal/ethapi/api.go:382-471（GetProof 对任意块 header.Root 开 trie 出证明） | 阻断性差距：Isthmus 提款证明（withdrawals.md 的 withdrawalProof 需要任意历史块的 MessagePasser 存储证明）在非创世块不可用；仅 genesis 块可出证明 |
| S-RPC-7 | "Withdrawals list in the block body is encoded as an empty RLP list."（OP 链 block body 的 withdrawals 恒为空列表） | isthmus/exec-engine.md:163 | MUST | Canyon+（Isthmus 基线） | ✅ | bcos-rpc/.../model/BlockResponse.cpp:109（`result["withdrawals"]` 恒空数组）；tools/op-e2e/rpc_matrix.py:196,207（e2e 断言恒 []） | internal/ethapi/api.go:1130-1132；core/genesis.go:675-681（EmptyWithdrawalsHash + 空列表） | RPC 面恒空数组与 op-geth 一致 |
| S-RPC-8 | "After Isthmus activation, 2 new fields `operatorFeeScalar` and `operatorFeeConstant` are added to transaction receipts if and only if at least one of them is non zero."（Isthmus 后回执加入 operatorFeeScalar/operatorFeeConstant，仅当至少一个非零） | isthmus/exec-engine.md:299-300 | MUST | Isthmus+ | ✅ | bcos-evm/.../opstack/OpTransition.cpp:253-258（fill_operator_scalars 且 scalar/constant 非零才写入 meta）；bcos-rpc/.../model/ReceiptResponse.cpp:118-121（nullopt 省略输出） | core/types/receipt.go:96-97（OperatorFeeScalar/OperatorFeeConstant）；internal/ethapi/api.go:1783-1788 | 与 op-geth 相同的有条件输出 |
| S-RPC-9 | "After Jovian activation, a new field `daFootprintGasScalar` is added to transaction receipts ... the `blobGasUsed` receipt field is set to the DA footprint of the transaction."（Jovian 回执加入 daFootprintGasScalar，blobGasUsed=交易 DA footprint） | jovian/exec-engine.md:147-149 | MUST | Jovian+ | ✅ | OpTransition.cpp:260-265（props.has_da_footprint 时写入 scalar 与 da_footprint=estimatedDaSizeFromFlz*scalar）；ReceiptResponse.cpp:122-125（daFootprintGasScalar 与 blobGasUsed=da_footprint） | internal/ethapi/api.go:1790-1794（DAFootprintGasScalar + blobGasUsed=receipt.BlobGasUsed） | da_footprint 数值本身属执行审计（M5），RPC 传输面正确 |
| S-RPC-10 | "From Jovian, the `blobGasUsed` property of each block header is set to that block's `daFootprint`."（Jovian 后块头 blobGasUsed=整块 DA footprint，RPC 需透出） | jovian/exec-engine.md:121-122 | MUST | Jovian+ | ✅ | BlockResponse.cpp:121-131（blockHeader->blobGasUsed() 有值则输出真值；PBFT/旧头 nullopt 时 0x0）；数据源 OpBlockExecute.cpp:305-316（seal.withdrawalsRoot/blobGasUsed 计算，执行侧） | internal/ethapi/api.go:1083-1085（head.BlobGasUsed 非 nil 时输出） | 执行侧 daFootprint 计算正确性属 M5 |
| S-RPC-11 | "Starting at Ecotone, the `parentBeaconBlockRoot` must be set to the L1 origin `parentBeaconBlockRoot`"；block JSON 需输出 baseFeePerGas/withdrawalsRoot/blobGasUsed/excessBlobGas/parentBeaconBlockRoot（EIP-4788/4844 + Isthmus 语义） | exec-engine.md:301-302,324,364；参照 op-geth api.go:1077-1091 | MUST | Ecotone+（Isthmus 基线） | ✅ | BlockResponse.cpp:98-108（baseFeePerGas 取自 header）、:110-120（withdrawalsRoot 取自 header，nullopt 时零值）、:125-132（blobGasUsed）、:133（excessBlobGas 恒 0x0，OP 链无 blob，与 op-geth 一致）、:136-143（parentBeaconBlockRoot 取自 header） | internal/ethapi/api.go:1077-1091 | v1 疑点"OP 字段仍为占位常量"已修复：全部改从 header 真值读取，仅 excessBlobGas 保留常量 0 |
| S-RPC-12 | （参照）op-geth 的 block JSON 输出 `requestsHash`（Prague 头字段，Isthmus 后恒 sha256('')，见 isthmus/exec-engine.md:75-77）；FISCO block JSON 缺该字段 | isthmus/exec-engine.md:75-77；参照 api.go:1092-1094 | SHOULD | Isthmus+ | 🟡 | BlockResponse.cpp 全文无 requestsHash 输出（缺失）；头值在创世/执行侧存在：Ledger.cpp:2011（setRequestsHash）、gen_eth_header_fixture.py:45,73 | internal/ethapi/api.go:1092-1094（`if head.RequestsHash != nil { result["requestsHash"] = ... }`） | 规范未强制 RPC 输出该字段，但与 op-geth 输出不一致；依赖该字段的工具（极少数）会读不到；共识侧值正确（sha256('')） |
| S-RPC-13 | （参照）op-geth 在 OP 链拒绝 blob（type-3）交易提交：`if b.ChainConfig().IsOptimism() && signedTx.Type() == types.BlobTxType { return types.ErrTxTypeNotSupported }`；FISCO 同策略 | 参照 eth/api_backend.go:332-335 | MUST | 全部 | ✅ | EthEndpoint.cpp:436-438（RawTransactionKind::Blob → InvalidParams "blob transactions are not supported"）；分类表 bcos-framework/.../engine/RawTransactionDispatch.h:47-78 | eth/api_backend.go:332-335 | FISCO 在 RPC 门与 mempool 门都拒绝，比 op-geth 更前置（op-geth 在池侧），无共识影响 |
| S-RPC-14 | 0x7E deposit 不得经 eth_sendRawTransaction 提交（deposit 只能由执行引擎/derivation 注入） | exec-engine.md:87（引申）；参照 api.go:1198（deposit 仅内部构造） | MUST | 全部 | ✅ | EthEndpoint.cpp:439-441（dispatch 分类门）+ :453-457（解码后类型门，双保险） | core/txpool/legacypool/legacypool.go:326-334（池拒绝） | 双门覆盖 RPC 与 in-process 两路径 |
| S-RPC-15 | eth_call/estimateGas 是只读模拟，不收取 operator fee/L1 fee（fee 只在真实交易执行中计费） | isthmus/exec-engine.md:263-298（EVM Fee Semantics 均针对真实交易）；参照 api.go:1054 | SHOULD | Isthmus+ | ✅ | EthEndpoint.cpp:566-664（call 仅构造 tx 交给 scheduler.call/callAtBlock 执行，无任何 OP 费注入逻辑）、:665-691（estimateGas 只返回 gasUsed） | internal/ethapi/api.go:1054（DoEstimateGas 不涉 fee 计算） | 执行侧是否在 call 模式下收取费用属 M5 执行审计 |
| S-RPC-16 | EIP-1186 eth_getProof 响应结构（address/balance/nonce/codeHash/storageHash/accountProof/storageProof） | 参照 internal/ethapi/api.go:462-470；withdrawals.md:164-184（提款证明输入含 storage proof） | MUST | 全部 | ✅ | EthEndpoint.cpp:1079-1144（六字段 + storageProof 逐槽 key/value/proof；inMPT 标记场景 A 排除槽为证空） | internal/ethapi/api.go:462-470（AccountResult） | 结构完整；可用性受限见 S-RPC-6 |
| S-RPC-17 | 提款证明流程本身（proveWithdrawalTransaction 的 outputRootProof/withdrawalProof 校验） | withdrawals.md:164-184 | MUST | 全部 | ➖ | 合约层（L1 OptimismPortal / L2 L2ToL1MessagePasser predeploy），不在 EL RPC 面；EL 侧支撑接口为 eth_getProof（S-RPC-6） | — | EL 侧相关能力见 S-RPC-6；合约字节码由 predeploys（bcos-l2-contracts / op-deployer allocs）提供 |

### S-GEN 组（创世/接线）

| 编号 | 规范要点（英文原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | op-geth 证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-GEN-1 | 创世 allocs 必须完整导入状态：balance/nonce/code/storage（op-deployer terminal allocs 是 OP 侧账户唯一来源） | 无专章；参照 core/genesis.go:169-178（hashAlloc 逐账户写入） | MUST | 全部 | ✅ | bcos-tool/bcos-tool/NodeConfig.cpp:215-307（loadAllocs：address 唯一、balance 十进制、nonce 限 uint64 :251-268、code 校验、storage 槽位 64 字符）；bcos-ledger/bcos-ledger/Ledger.cpp:1838-1967（importGenesisState：建账户/写 code/nonce/balance/storage） | core/genesis.go:169-178 | 零值槽不写盘（build-allocs.py:238-240），与规范零槽不存在语义一致 |
| S-GEN-2 | 创世 stateRoot 必须等于 allocs 的 MPT 根，且 artifact 头校验（错误 artifact 不得锚定链） | 参照 core/genesis.go:180-190（stateRoot=Commit(alloc)） | MUST | 全部 | ✅ | Ledger.cpp:1979-1986（applyEthGenesisHeader 拒绝 artifact 与本地派生根不一致）；tools/opstack-genesis/mpt_state_root.py:213-231（独立 keccak MPT 计算与 C++ 对拍）；tools/opstack-genesis/gen_eth_header_fixture.py:17-22 | core/genesis.go:180-190 | 双实现（python/C++）独立计算同一根 |
| S-GEN-3 | "If Isthmus is active at genesis block, the `withdrawalsRoot` in the genesis block header is set to the L2ToL1MessagePasser account storage root."（Isthmus 创世 withdrawalsRoot=MessagePasser 存储根） | isthmus/exec-engine.md:100-101；参照 core/genesis.go:711-719 | MUST | Isthmus+ | 🟡 | Ledger.cpp:2007（setWithdrawalsRoot 取自 artifact）；工具链默认 gen_eth_header_fixture.py:69（withdrawals_root=EMPTY_TRIE_ROOT）；tools/op-e2e/rpc_matrix.py:199-219（e2e 断言 genesis withdrawalsRoot=空根，因 Phase A 的 MessagePasser 为直接部署空存储） | core/genesis.go:184-190,711-719（Isthmus 时 GetStorageRoot(MessagePasser)，并告警空根） | 偏差：Phase A 中 MessagePasser 非代理且无存储 → 空根；spec 假定 proxied predeploy 恒非空。root=实际存储根等式成立，但若后续按 op-deployer 布局部署代理，必须重生成 artifact（工具支持覆盖）；工具链不自动计算 MessagePasser 根，靠部署方提供 |
| S-GEN-4 | 创世 parentBeaconBlockRoot 为零（EIP-4788：创世无父块） | 参照 core/genesis.go:683-686 | MUST | Cancun+（基线） | ✅ | gen_eth_header_fixture.py:72（默认零）+ Ledger.cpp:2010（setParentBeaconBlockRoot） | core/genesis.go:683-686 | — |
| S-GEN-5 | 创世 requestsHash=sha256('')（EIP-7685 空请求哈希，Isthmus 恒为空） | isthmus/exec-engine.md:77；参照 core/genesis.go:702-704 | MUST | Isthmus+ | ✅ | gen_eth_header_fixture.py:44-45,73（EMPTY_REQUESTS_HASH=0xe3b0c4...）+ Ledger.cpp:2011 | core/genesis.go:702-704 | 与规范常量一致 |
| S-GEN-6 | 创世 block body withdrawals 为空列表（EmptyWithdrawalsHash） | 参照 core/genesis.go:675-681 | MUST | Canyon+（基线） | ✅ | BlockResponse.cpp:109（恒空数组）；rpc_matrix.py:196,207 | core/genesis.go:675-681 | — |
| S-GEN-7 | op-deployer 生成的 terminal allocs 为 OP 侧（0x42.../0xc0d3...）账户唯一来源，FISCO overlay 只做自写 predeploy 叠加且禁止地址冲突；expected_predeploys 断言基础 allocs 含所需 predeploy | 无专章（op-deployer 工具链）；交叉核对 op-geth allocs 语义 core/genesis.go:169-178 | MUST | 全部 | ✅ | tools/opstack-genesis/build-allocs.py:1-48（模块契约）、:234-273（load_base_allocs）、:596-621（expected_predeploys 断言 + overlay 冲突硬错）、:682-720（CLI：--base-allocs 必填、base_allocs_sha256 溯源 :207-231） | core/genesis.go:169-178（alloc JSON 结构） | 溯源 pin（sha256）防止漂移 |
| S-GEN-8 | feature_flags：SystemConfig 的 feature_flags Entry 必须写入创世存储并等于节点实际 feature 集（规范侧为 SystemConfig setFeatureEnabled/isFeatureEnabled 合约入口；FISCO 以 genesis 槽注入等效终态） | system-config.md:227-241（合约函数）、:162-170（Customizable Feature 定义） | MUST | 全部 | ✅ | build-allocs.py:39-44,166,551-557（feature_flags 为 SystemConfig overlay 必填输入，槽=keccak256("feature_flags"\|\|be32(101))）；Ledger.cpp:1900-1964（创世验证槽存在且==Features::toFlagsNumber()，不再注入） | —（合约行为，EL 侧无对应） | 文档与实现一处不一致：chain-config.yaml 模板注释仍写"feature_flags 由 C++ 注入"，实际已改为验证（Ledger.cpp:1905-1907 注释明确），建议更新模板 |
| S-GEN-9 | 分叉接线：feature_op_jovian 驱动 Jovian（DA footprint、operatorFee×100），否则 Isthmus 基线 | 参照 jovian/exec-engine.md:167-174（公式更新）与 isthmus 配置 | MUST | 全部 | ✅ | bcos-framework/.../ledger/Features.h:116,125（feature_l2_ethereum_compat=57、feature_op_jovian=60）；bcos-tool/.../NodeConfig.cpp:448-456（opJovianActive 读 genesis [features]）；bcos-evm/.../opstack/OpForkSchedule.cpp:103-110（configAt：jovianActive→Jovian 否则 Isthmus；:58-86 两配置的 has_operator_fee/has_jovian_operator_formula/has_da_footprint 齐全） | —（op-geth 用时间戳链配置） | FISCO 用 feature flag 替代时间戳是本地设计（决策 A5），语义等价 |
| S-GEN-10 | 创世 allocs 确定性输出（排序稳定、全宽 32 字节槽、十进制 quantity），保证 FISCO/op-reth oracle 同根 | 交叉核对 op-geth genesis 解析 | SHOULD | 全部 | ✅ | build-allocs.py:622（按地址升序）、:625-649（emit_ini 槽升序全宽字）、:652-679（emit_alloc_json 供 op-reth oracle） | — | 保证跨客户端创世根一致 |

## 判定计数

- 条款总数：27（S-RPC 17 条 + S-GEN 10 条）
- 判定：✅ 23、🟡 2、❌ 1、➖ 1（S-RPC-17 为合约层，不适用）

## 重点差距清单

1. **S-RPC-6（❌，最重）**：eth_getProof 在非创世块不可用（MPT 节点仅 genesis 导入写盘，Ledger.cpp:2398；rpc_matrix.py:221-224 自认 -32004/-32602）。影响：Isthmus 消息证明（提款 withdrawalProof、outputRoot 验证）在任意历史块无法生成/校验，用户无法证明提款，与 withdrawals.md/isthmus/exec-engine.md:58-59 直接冲突。
2. **S-RPC-12（🟡）**：block JSON 不输出 requestsHash（op-geth api.go:1092-1094 输出）。影响：与参照输出不一致，依赖该字段的工具读不到；共识侧值正确（sha256('')）。
3. **S-GEN-3（🟡）**：Isthmus 创世 withdrawalsRoot 工具链默认空根（Phase A MessagePasser 直接部署空存储），spec 假定 proxied 恒非空。影响：root=实际存储根等式成立故共识自洽，但一旦切换到 op-deployer 代理布局必须重生成 artifact；工具不自动推导 MessagePasser 根，依赖部署方填值。

## v1 疑点复核结论

- BlockResponse 的 OP 字段占位常量 → **已修复**：baseFeePerGas/withdrawalsRoot/blobGasUsed/parentBeaconBlockRoot 均从 header 真值读取（BlockResponse.cpp:98-143）；excessBlobGas 恒 0x0 与 op-geth 一致（OP 链无 blob）。
- ReceiptResponse 是否输出 OP 元数据 → **是**：13 字段全输出（ReceiptResponse.cpp:104-131），含 l1GasPrice/l1Fee/operatorFeeScalar/operatorFeeConstant/daFootprintGasScalar/blobGasUsed(da_footprint)/depositNonce/depositReceiptVersion；另有 FISCO 扩展 operatorFee（op-geth 无此字段，多余字段对客户端无害）。
- deposit tx JSON nonce 硬编码 0x0 → **已修复**：mined deposit 用回执 depositNonce 覆盖（TransactionResponse.cpp:24-31）。
- OpStackReceiptMeta 是否零调用方 → **有调用方**：ReceiptResponse.cpp:104、TransactionResponse.cpp:27、TransactionReceiptImpl opStackMeta()/setOpStackMeta()（bcos-tars-protocol/.../TransactionReceiptImpl.cpp:213-252）。

## 存疑点

1. deposit tx 的 RPC JSON 不输出 depositReceiptVersion（仅回执输出；op-geth api.go:1210-1213 在 tx 响应也输出）——与参照的小差异，未单列条款，视需要可补。
2. S-GEN-8 中 chain-config.yaml 模板注释与 Ledger.cpp 行为不一致（"注入"vs"验证"），纯文档问题。
3. deposit_nonce/deposit_receipt_version 的 tars 哈希域非 EIP-2718 RLP，跨客户端 receiptsRoot 一致性取决于 M5 的回执编码（FISCO 回执根为 tars 哈希），本子任务仅在传输面确认字段存在。


## 模块：m7-karst-overview

# M7：Karst 逐条审计 + Delta/Monsoon/Lagoon 及新目录概览扫描

- **模块**：M7（Karst 逐条 + Delta/Monsoon/Lagoon 概览）
- **负责章节**：`/tmp/op-specs/specs/protocol/karst/{overview.md, exec-engine.md, derivation.md}`（逐条）；`delta/{overview.md, span-batches.md}`、`monsoon/overview.md`、`lagoon/{overview.md, post-exec.md, sdm.md}` 及 `/tmp/op-specs/specs/protocol/` 下其他新目录/文件（概览）
- **Spec 基线**：ethereum-optimism/specs @ 2049036afe878a7cb443f513f4e6ca453d90c340（/tmp/op-specs）
- **FISCO 代码**：worktree `/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/op-alignment`（分支 feat-opstack-e2e，工作区未提交改动以文件当前内容为准；全程只读）
- **op-geth commit**：d0734fd5f44234cde3b0a7c4beb1256fc6feedef（/Users/octopus/octo/code/op-geth，optimism 分支本地源码）
- **op-reth 拉取情况**：`git clone --depth 1 https://github.com/paradigmxyz/reth /tmp/op-reth-ref` 成功，commit **7b3432d950030875b23c5c4df7c9f53a7caa4868**（reth v2.5.1 main）——**该版本已移除全部 OP 客户端代码**（`crates/optimism` 不存在，全仓 grep 无 jovian/karst/fjord 命中）；reth v1.9.4（tag ce2b369595b6503a1d47fee946a8dc37fa525ed5，raw 拉取无 commit）有 OP 支持但**最远到 Jovian，无 Karst**。结论：**Karst 未合入任何已发布的 EL 客户端**。
- **op-node 参照（raw 拉取无 commit，ethereum-optimism/optimism develop 分支）**：`op-core/nuts/bundles.go` 已嵌入 `KarstNUTBundleJSON`（bundles.go:7-10）；`op-node/rollup/derive/upgrade_transaction.go` 已有通用 NUT 机制并含 `case forks.Karst:` 分支（:90, :125-127）。Karst NUT 的 CL 侧参照以 op-node develop 为准。

---

## 一、Karst 逐条审计（条款编号 S-KAR-1 起）

> 背景核对：FISCO 自述 Karst 为 Jovian 占位别名（OpForkSchedule.cpp:88-101）。**核实结论：spec 的 Karst 有大量独立于 Jovian 的执行语义**（bn256Pairing 输入上限降至 57,600B、Osaka EIP 包、derivation 注入 31 笔 NUT 升级交易），与"Jovian 别名"存在本质差异——别名只覆盖 Jovian 语义，Karst 专属条款全部缺失。

| 编号 | 规范要点（英文原文关键句 + 中文释义） | 来源（文件:行号） | MUST/SHOULD | 适用分叉 | 判定 | FISCO 证据（文件:行号） | 参照实现证据（文件:行号） | 备注 |
|---|---|---|---|---|---|---|---|---|
| S-KAR-1 | "The `bn256Pairing` precompile input size is reduced from the Jovian limit of 81,984 bytes (427 pairs) to 57,600 bytes (300 pairs)" —— bn256Pairing 预编译输入上限从 Jovian 的 81,984B 降到 57,600B；"The other variable-input precompile limits are unchanged from Jovian" | karst/exec-engine.md:16-18, 25 | MUST | Karst | ❌ | OpPrecompiles.cpp:35-41（kJovianEntries 中 bn256Pairing 上限仍为 81984；无 Karst 专属表）；OpForkSchedule.cpp:93-101（karstConfig() = jovianConfig() 别名，precompiles 指针 = &jovianPrecompileOverrides()）| **无任何客户端实现 57,600**：op-geth params/protocol_params.go:192 仅有 Bn256PairingMaxInputSizeJovian=81984，全仓 grep 无 57600；op-reth main 无 Karst（v1.9.4 亦无）| 全部四个可变输入限制（G1/G2 MSM 288,960/278,784、pairing 156,672）Jovian 值与 spec 一致 ✅，仅 bn256 一条为 Karst 差异 |
| S-KAR-2 | "EIP-7823: Set upper bounds for MODEXP" —— MODEXP 输入长度上限（base/mod/exp 均 ≤ 1024）| karst/overview.md:19 | MUST | Karst | 🟡 | EVM 层已实现：precompiles.cpp:42（MODEXP_LEN_LIMIT_EIP7823=1024）、:179-181（`rev < EVMC_OSAKA ? u32max : EIP7823 上限`）；**但 OP 路径不生效**：OpForkSchedule.cpp:97 使 karstConfig().rev = EVMC_PRAGUE（Jovian 别名），EVMC_OSAKA 门控永不命中 | op-geth core/vm/contracts.go:738（`c.eip7823 && max(baseLen,expLen,modLen) > 1024` 拒绝）| EVM 基础设施就绪，缺"Karst → EVMC_OSAKA"的 fork 绑定 |
| S-KAR-3 | "EIP-7825: Transaction Gas Limit Cap (not enabled for deposits, which are already subject to a 20MGas limit)" —— 普通交易 gasLimit 上限 2^24；deposits 豁免 | karst/overview.md:20-21 | MUST | Karst | 🟡 | EVM 层已实现：state.cpp:387（`rev >= EVMC_OSAKA && tx.gas_limit > MAX_TX_GAS_LIMIT` 拒绝）、transaction.hpp:17（MAX_TX_GAS_LIMIT=0x1000000=2^24，与 EIP 值一致）；deposit 豁免 ✅：runDeposit 走 OpTransition.cpp:488-560，不经过该检查（:530 仅 blockGasLeft 检查）→ **OP 路径 rev=PRAGUE 时对普通 tx 亦不生效** | op-geth params/protocol_params.go:42（`MaxTxGas = 1 << 24`）| 常量值与 EIP 一致；仅缺 OP 路径的 Osaka 绑定 |
| S-KAR-4 | "EIP-7883: ModExp Gas Cost Increase" —— MODEXP 费用上调（factor 8→16、min_gas 200→500、EIP-7883 乘法复杂度公式）| karst/overview.md:22 | MUST | Karst | 🟡 | EVM 层已实现：precompiles.cpp:127（`factor = rev < EVMC_OSAKA ? 8 : 16`）、:155-166（`rev >= EVMC_OSAKA → {500, 1, calc_mult_complexity_eip7883}`）—— OP 路径不生效（同 S-KAR-2 的 rev 问题）| op-geth core/vm/contracts.go:712（eip7883 分支）| 同上 |
| S-KAR-5 | "EIP-7910: eth_config JSON-RPC Method" —— 新增 eth_config RPC 方法（返回执行层配置）| karst/overview.md:23 | SHOULD（节点接口，非共识）| Karst | ❌ | FISCO RPC 层无 eth_config：bcos-rpc/bcos-rpc/web3jsonrpc/ 下全部 eth_ 方法仅 eth_call/eth_estimateGas/eth_getBalance/eth_getCode/eth_getProof/eth_getStorageAt/eth_getTransactionCount（+eth_subscription），无 Config 方法 | op-geth internal/ethapi/api.go:1408（`// Config implements the EIP-7910 eth_config method`）| 非共识条款；接口面缺失 |
| S-KAR-6 | "EIP-7939: Count leading zeros (CLZ) opcode" —— 新增 CLZ 指令（0x1e）| karst/overview.md:24 | MUST | Karst | 🟡 | evmone 已实现：vcpkg/packages/evmone_arm64-osx/include/evmone/instructions_traits.hpp:177（`table[EVMC_OSAKA][OP_CLZ] = 5`）、:258（`{"CLZ", 0, false, 1, 0, EVMC_OSAKA}`）、instructions_opcodes.hpp:44（OP_CLZ=0x1e）—— OP 路径 rev=PRAGUE 不生效 | op-geth core/vm/eips.go:44（7939: enable7939）、:299-300（opCLZ）| 同上 |
| S-KAR-7 | "EIP-7951: Precompile for secp256r1 Curve Support (this has been present since Fjord, but the gas cost will be updated to align with Ethereum)" —— 0x100 P256Verify 自 Fjord 已存在，Karst 起 gas 与 Ethereum 对齐（3450→6900）| karst/overview.md:25-26 | MUST | Karst | 🟡 | EVM 层 0x100 的默认 gas=6900 与 EIP 一致（precompiles.cpp:292-294 p256verify_analyze 返回 6900；:807 表项 `{0x0100, EVMC_OSAKA, ...}`）；**但 OP 路径被 override 钉死在 3450**：OpPrecompiles.cpp:29,37（kJovianEntries/kIsthmusEntries 中 `gas_cost_override = 3450`）、OpHost.cpp:142-146（executeGasOverridePrecompile 消费 override）→ karstConfig() 继承 3450，无 6900 语义 | op-geth params/protocol_params.go:183-184（P256VerifyGasFjord=3450 / P256VerifyGas=6900）；Karst 绑定 6900 无客户端实现 | FISCO 需在 Karst 配置中取消/替换 3450 override |
| S-KAR-8 | "EIP-7642: eth/69 - history expiry and simpler receipts" —— eth p2p 协议变更（历史区块范围通告、简化 receipts 传输、BlockRangeUpdate 消息）| karst/overview.md:18 | MUST（p2p 层）| Karst | ➖ | FISCO 无 eth p2p（自研 FISCO 网络），无 eth/69 面 | op-geth 未实现（grep 7642/eth69 无命中）；EIP 原文确认"does not change consensus rules of the EVM and does not require a hard fork" | 无 EL 共识影响；仅当 FISCO 后续提供 eth p2p 时适用 |
| S-KAR-9 | "Network upgrade transactions applied during derivation" —— CL 综述：Karst 激活时 derivation 注入 NUT | karst/overview.md:30 | MUST | Karst | ❌ | FISCO 无任何 NUT/升级交易机制：opstack-executor 与 bcos-evm/opstack 全目录 grep upgrade/NUT 无实现命中（仅测试注释提及 ecotone 激活）；OpScheduler.h 无激活区块/升级注入逻辑 | op-node develop（raw）：op-node/rollup/derive/upgrade_transaction.go:18-21（NUT 从 JSON 读取）、:125-127（`case forks.Karst: bundleJSON = nuts.KarstNUTBundleJSON`）| 需 EL/CL 协同的整块能力 |
| S-KAR-10 | "The first block with a timestamp at or after the Karst activation time is considered the Karst activation block" —— 首个 timestamp ≥ karstTime 的区块为激活区块 | karst/derivation.md:13 | MUST | Karst | ❌ | FISCO 无 timestamp fork 激活机制：Features.h:125-129 注释明言"FISCO has no timestamp-based fork activation"；Initializer.cpp:570-574（OP fork 仅由 feature_op_jovian 布尔开关选择）；无 feature_op_karst / karstTime | op-node develop：forks.Karst 时间戳条件（fork 配置层）| 需新增 feature flag 或 karstTime 映射 |
| S-KAR-11 | "On the Karst activation block, in addition to the L1 attributes deposit and potentially any user deposits from L1, a set of deposit transaction-based upgrade transactions are deterministically generated by the derivation pipeline" —— 激活区块上确定性生成 NUT（位于 L1 attributes deposit 之后、用户交易之前）| karst/derivation.md:15-16 | MUST | Karst | ❌ | 无实现（同 S-KAR-9）；FISCO 区块执行按普通 deposit 处理（OpstackExecutor.h:530-556 deposit 分支），无注入源 | op-node upgrade_transaction.go:90-100（bundle → deposit txs）；执行顺序见 l2-upgrades-1-execution.md:234-250 | FISCO executeDeposit（OpTransition.cpp:488）可执行任意 deposit，缺的是生成/注入与顺序编排 |
| S-KAR-12 | "The contents of the transaction are defined by karst_nut_bundle.json … which contains 31 transactions. In addition …, the consensus layer node MUST prefix each intent string with `Karst <index>:` followed by a space" —— bundle 含 31 笔；CL 节点 MUST 给每笔 intent 加 "Karst <index>:" 前缀 | karst/derivation.md:18-22 | MUST | Karst | ❌ | bundle 未嵌入 FISCO（grep nut/karst_bundle 无命中）；前缀影响 UpgradeDepositSource 的 sourceHash（共识关键） | op-node develop upgrade_transaction.go:90（`qualifiedIntent := fmt.Sprintf("%s %d: %s", capitalizeForkName(b.ForkName), i, nutTx.Intent)` 与 spec 格式一致）；bundle 实测：/tmp/karst_nut_bundle.json 31 笔（metadata.version=1.0.0）| 判定 ❌ 依据 FISCO 无实现；op-node 侧 spec 与实现已对齐 |
| S-KAR-13 | "These transactions can be classified into the following groups: 1 ConditionalDeployer Deployment; 1 ConditionalDeployer Upgrade; Implementation Deployments (26 total); 1 ProxyAdmin Upgrade; 1 L2ContractsManager Deployment; 1 Upgrade Execution" —— 31 笔的分组与顺序 | karst/derivation.md:24-32 | MUST | Karst | ❌ | 无实现 | 实测 bundle 分组与 spec 完全一致：idx0=部署、idx1=升级、idx2-27=26 个实现部署、idx28=ProxyAdmin 升级、idx29=L2ContractsManager、idx30=upgradePredeploys（/tmp/karst_nut_bundle.json；发送者 0xDeaD…0001/零地址两种，gasLimit 49,711~6,251,000）| spec 与 bundle 事实核对 ✅，FISCO 侧 ❌ |
| S-KAR-14 | "More information regarding the upgrade path … in L2 Upgrade Execution" —— 引用 l2-upgrades-1-execution.md：NUT 原子性/确定性/可验证（iUP-001/002/004）、格式约束（iNUTB-001~005）、激活区块自定义 gas 分配 = Σ bundle gasLimit（iUBGL-001~004 + Gas Allocation Specification，含后续块恢复与重建时回退）| karst/derivation.md:34 + l2-upgrades-1-execution.md:163-171, 343-405, 527-623 | MUST | Karst | ❌ | 无实现（FISCO 无 gas 分配/恢复机制；OpBlockExecute.cpp 的 gasUsed 计算无 NUT 概念）| op-node develop upgrade_transaction.go:115-142（totalGas/UpgradeTransactions/UpgradeGas 提供 Σ gasLimit）；spec 引 kona-protocol to_system_config 作恢复参照 | 激活区块 gasLimit 提升 + 次块恢复 + SystemConfig 重建回退，整块缺失 |

**Karst 判定统计：14 条 —— ✅ 0 / 🟡 5（S-KAR-2,3,4,6,7）/ ❌ 8（S-KAR-1,5,9,10,11,12,13,14）/ ➖ 1（S-KAR-8）**

核心结论：`karstConfig()` 别名（OpForkSchedule.cpp:93-101）+ `configAt()` 永不返回 Karst（:103-110）+ 无 feature_op_karst（Features.h:125）三重占位，Karst 在 FISCO 当前不可达且无任何专属语义。EVM 层（evmone 定制版）已具备 EVMC_OSAKA 与全部 Osaka EIP 原语（EIP-7823/7825/7883/7939/7951 的以太坊值），差距集中在**OP fork 绑定（rev=EVMC_OSAKA）与 NUT 机制**。

---

## 二、概览扫描表（Delta / Monsoon / Lagoon / 其他新目录）

| 章节 | 关键差异（EL 侧相关度） | FISCO 现状 | 判定 |
|---|---|---|---|
| delta/overview.md | 纯 CL 升级：delta_time 激活规则，仅 rollup-node | 不涉及 EL | 逐条范围外（CL 侧）；FISCO 无 rollup-node 面 |
| delta/span-batches.md | batch 格式 v1（span-batch 编码/校验，MAX_SPAN_BATCH_ELEMENT_COUNT）与激活规则；v2（fee_recipients）为实验性未激活 | 不涉及 EL 执行语义（batch 解码在 CL；batch 内的 tx 仍是标准 EIP-2718，含 Isthmus 的 type-4 SetCode——FISCO EVM 已支持 type-4）| 逐条范围外（CL/batcher 侧）|
| monsoon/overview.md | **空壳**：EL 与 CL 章节均为空，无任何条款 | 无 | 无 EL 要求 |
| lagoon/overview.md | EL = post-exec.md + sdm.md + interop/tx-pool；CL/合约 = interop 全套 | 见下行 | 需 EL 实现（大特性，逐条审计另行裁定）|
| lagoon/post-exec.md | 新 EIP-2718 类型 **0x7D** post-exec tx：无签名/无 gas、必须为区块最后一笔、至多一笔、payload `[version, blockNumber, ...]` 锚定、blockNumber 必须等于所在区块号、schema 必须已激活；receipt 为 EIP-1559 同构（status=1、空 logs、全零 bloom、cumulativeGasUsed 继承前一笔）；DA footprint 计入时跳过 0x7D（Jovian daFootprint 修改，jovian/exec-engine.md:98-126）；mempool/传播禁入 | FISCO 无 0x7D 概念：OpCommon.h:82-95 classifyTxType 仅识别 0x7e/legacy/0x01/02/04，未知类型透传；无 post-exec 执行/收据/DA 排除逻辑；无"最后位置"结构规则 | **需 EL 实现**（新 tx 类型 + 收据 + 区块结构规则 + DA footprint 例外），工程量中等；暂不逐条 |
| lagoon/sdm.md | SDM v1 schema：gasRefundEntries（index/gasRefund、严格递增、非零、仅标准 tx）、refund(i) ≤ evmGasUsed(i)、canonicalGasUsed 写入收据与累计、settlement（sender 贷记 r*p+ΔoperatorFee；beneficiary 借记 r*(p-b)；base fee vault 借记 r*b；operator fee vault 借记 ΔoperatorFee）、原子结算、RPC 收据扩展字段 opGasRefund（不进 trie）| 无实现（FISCO 收据模型 bcos::protocol::TransactionReceipt 无 SDM 字段；无结算路径）| **需 EL 实现**（执行后结算 + 收据 gas 改写 + RPC 字段）；依赖 post-exec，逐条审计另行裁定 |
| lagoon/sdm.md 激活 | 激活时间前 block 不得含 0x7D；SDM 与 Lagoon 同门控 | FISCO 无 Lagoon/0x7D 门控 | 同上行 |
| orogeny/overview.md | 空壳（EL/CL 章节空）| 无 | 无 EL 要求 |
| permafrost/overview.md | 空壳（EL/CL 章节空）| 无 | 无 EL 要求 |
| narrows/overview.md | 空壳（EL/CL 章节空）| 无 | 无 EL 要求 |
| pectra-blob-schedule/ | 可选 CL 特性：blob base fee 更新分数在 fork 时间戳前保留 Cancun 值（反向激活）；derivation.md 为 L1 blob 调度 | 不涉及 EL | 逐条范围外（CL）|
| custom-gas-token/ | EL 侧 = predeploy 合约行为（systemConfig.isFeatureEnabled(CUSTOM_GAS_TOKEN) 位图开关）；新 predeploy：NativeAssetLiquidity、LiquidityController、L1BlockCGT、L2ToL1MessagePasserCGT 等；ETH 桥接禁用语义 | FISCO 无 CGT predeploy 集与 feature 开关（OpPredeploys.h 无相关地址/代码；注意 Karst bundle 恰好部署 L1BlockCGT/L2ToL1MessagePasserCGT/LiquidityController/NativeAssetLiquidity，CGT 与 Karst 绑定）| **需 EL 实现**（predeploy 地址/代码 + L1Block 语义），执行语义主要靠合约代码，工程量中等；暂不逐条 |
| guaranteed-gas-market.md | L1 侧 deposit gas 市场（OptimismPortal 合约：MAX_RESOURCE_LIMIT=20M、EIP-1559 式 deposit base fee）；L2 端仅沿用"deposit 已买 gas、不可退" | 不涉及 EL 执行语义变化（FISCO deposit 执行路径不变）| 逐条范围外（L1 合约）|
| l2-upgrades-1-execution.md | NUT 机制与自定义升级块 gas（Σ bundle gasLimit 提升激活块 gasLimit、次块恢复、SystemConfig 重建回退、失败即链停）；执行顺序 L1 deposit → 6 组 NUT → 用户交易 | 无 NUT/gas 分配机制（见 Karst S-KAR-9~14）| **需 EL 实现**（与 Karst NUT 同一条线，已被 S-KAR-9~14 覆盖）|
| l2-upgrades-2-contracts.md | 占位重定向文件（已迁 monorepo packages/contracts-bedrock/specs/）| 无 | 无独立条款 |
| safe-extensions.md / stage-1.md | L1 治理合约（Liveness Guard/Module、Withdrawal Liveness/Safety、Pause 机制）| 不涉及 EL | 逐条范围外（L1 合约）|
| superchain-upgrades.md | 协议版本信令（ProtocolVersions 合约）、timestamp 激活规则、升级时间表 | CL 配置层；FISCO 无协议版本信令面 | 逐条范围外（CL）|
| proposals.md | L2OutputOracle L1 合约 + output commitment 构造（EL 只负责产出 output root，FISCO 已有 stateRoot 承诺）| EL 侧无新要求 | 逐条范围外 |
| flashblocks.md | sequencer 侧 Rollup Boost 外部构建器 + fallback EL 架构（可选组件）| 不涉及核心 EL 协议（FISCO 是验证/执行客户端）| 逐条范围外（可选架构）|
| revshare/ | L1 合约（FeesDepositor/L1Withdrawer/SuperchainRevSharesCalculator）| 不涉及 EL | 逐条范围外（L1 合约）|

**概览要点总结**：Delta 纯 CL（span-batches 无 EL 影响）；Monsoon 空壳；Lagoon 是 EL 侧最大新增面（0x7D post-exec + SDM 结算 + interop tx-pool），FISCO 全部缺失；其他新目录除 custom-gas-token（predeploy 集）与 l2-upgrades-1-execution（NUT/gas）外均无 EL 执行语义要求。

---

## 三、FISCO 现状关键取证行号汇总

- OpForkSchedule.h:41-50（OpFork 枚举含 Karst）、:54-64（OpForkConfig：rev/precompiles/各语义开关）、:72（karstConfig 声明）、:82-87（OpForkFlags 仅 jovianActive）、:95（configAt）
- OpForkSchedule.cpp:88-101（karstConfig = jovianConfig 别名，注释自认占位）、:103-110（configAt 永不返回 Karst）
- OpPrecompiles.cpp:35-41（kJovianEntries：bn256=81984、G1=288960、G2=278784、pairing=156672、p256=3450）、:61-65
- Features.h:125-129（feature_op_jovian=60；无 karst flag）；NodeConfig.cpp:449-459（opJovianActive 仅查 feature_op_jovian）；Initializer.cpp:570-574（OpForkFlags 组装）
- OpScheduler.h:588、:939（configAt(m_forkFlags)）；OpstackExecutor.h:430-437（默认 forkConfig=jovianConfig）、:701-708（rev 不匹配抛 OpForkRevisionMismatch）
- LedgerConfig.h:270（EVMC_REVISION_DEFAULT=EVMC_OSAKA）；state.cpp:340（blob 数上限）、:387（EIP-7825 gas cap）；transaction.hpp:17（MAX_TX_GAS_LIMIT=2^24）
- precompiles.cpp:42、:127、:155-166、:179（EIP-7823/7883）、:292-294（p256verify=6900）、:807（0x0100 @EVMC_OSAKA）
- instructions_traits.hpp:177、:258（CLZ @EVMC_OSAKA，evmone 包）；OpHost.cpp:142-146（gas override 消费）、:166-169（0x100 预热 @Isthmus）
- OpTransition.cpp:488-560（runDeposit：:494-495 拒绝 is_system_tx、:530 blockGasLeft）
- OpBlockExecute.cpp:244-270（encodeReceiptForRoot，EIP-658 全字段 receipts trie）
- bcos-rpc/bcos-rpc/web3jsonrpc/（eth_ 方法仅 7 个，无 eth_config）

