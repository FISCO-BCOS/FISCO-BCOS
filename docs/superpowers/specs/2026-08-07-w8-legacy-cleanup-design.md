# W8：记忆遗留清理设计（ctest 复测 / 落盘核实 / 四项决策）

> 目标：收口早期重构 session 遗留的 3 项（#2 全量 ctest 复测 / #4 `s_number_2_header` 落盘核实 / #5 四项决策），让整体状态干净。
> 状态：**Approved**。日期：2026-08-07。
> 关联：记忆 `op-executor-core-recovery`（#2/#4/#5）、`op-validator-loop-status`（四项决策）、`typed-tx-corpus-yparity-plan`（C2）。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

对比主线（W1-W7）已全部交付。早期重构 session（`op-executor-core-recovery`）遗留 3 项未收口：

| 项 | 问题 | 性质 |
|---|---|---|
| #2 | v2 重植后 engine/rpc/ledger/tars-protocol/executor 重建未全量 ctest | 验证 |
| #4 | `s_number_2_header` 落盘路径「待查」 | 核实 |
| #5 | 四项决策：C1 前导零位置 / C2 yParity 位宽 / 语料重生成 / C6 硬拒定性 | 裁定+修复 |

W8 把这 3 项收口——验证、核实、裁定、修复，让对比主线之后的整体状态干净。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | **三项全做**（#2/#4/#5），一个计划每项独立 task | 用户选定 |
| **D2** | **#5 裁定边界**：W8 用代码证据裁定 C1/C2/C6 + 语料重生成；**C1 修法用户拍板** | 用户选定 |
| **D3** | ⚠️ W8 审查（R2）修正：**C1 不是从零调查**——上游 `feat-op-validator-loop` 已拍板「Plan A：修共享」并落地（`a37517327`/`245f47f0c`/`4e0848e2f`），本分支缺该修复且 `OpSchedulerImpl.h:277-278` 注释失实（声称已修）。T3 = 确认/搬运已批准修复 + 重验 + 修注释 | W8 审查 |
| **D4** | ⚠️ W8 审查（R3）修正：**语料重生成无意义**（currentRandom/coinbase 恒值是生成器结构性保证，重生成字节等同）；T5 降级为**可复现性验证**（regen.sh 跑通即收口） | W8 审查 |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| 相关模块源码（#2 复测发现的问题） | 视复测结果 |
| `s_number_2_header` 相关（#4） | 视核实结果 |
| `bcos-codec/rlp/RLPDecode.h` 或 OP 侧（#5-C1） | 视用户拍板 |
| `bcos-rpc/.../Web3TxHandler.cpp` + 测试（#5-C2） | yParity 校验 |
| legacy/0x01 处置（#5-C6） | 视定性 |
| `bcos-evm/test/opstack/t8n/vectors/`（#5-语料重生成） | 重生成 + SHA256SUMS |

## 4. 各 task 设计

### T1 — #2 全量 ctest 复测
- **对象**：engine(11) / rpc(185) / ledger(187) / tars-protocol(1) / bcos-executor(363) / opstack-executor(9 gtest) / ethereum-executor(未注册 ctest) / transaction-executor(173) / transaction-scheduler(113) 相关模块（⚠️ R1：ethereum-executor 无 ctest 注册，OP 专属 `bcos-evm-opstack-tests` 需单列）
- **方法**：`cd build && ctest` 全量回归（⚠️ 必须进 build/，仓库根跑 ctest 见 0）
- **基准口径**（⚠️ R1 修正）：「release-3.18 全量 1857/1858」标签失实（是 post-sync 特性分支计数，非纯净 release）；当前分支全量 1909。**改「当前分支 1909 全绿 + 记忆基准 1857/1858 只做比例参考」**；WsToolsTest 本机通过（环境相关，非确定性失败）
- **产出**：回归报告（全绿 / 失败清单 + 修复）
- **验证**：相关 target 全绿（全量 1909 或明确记录的失败清单）

### T2 — #4 s_number_2_header 落盘核实
- **对象**：`SYS_NUMBER_2_BLOCK_HEADER` 表（LedgerTypeDef.h:101）的 OP header 落盘链路
- **方法**（⚠️ R1 补最关键一环——commit/merge 时机）：
  1. 写侧：`registerOpBlock`（EngineServiceImpl.h:1214-1220）写 tars BlockHeader 字节——格式确认
  2. **提交路径（核心）**：`EngineServiceImpl.h:1167` `pushView`——**OP 路径是否 `mergeBackStorage()`？**（⚠️ 答案：否——OP 提交从不 merge，s_number_2_header 行只存内存 view 层，未落后端 RocksDB。这是记忆 #4「待查」的实质）
  3. 读侧：EngineServiceImpl.h:897（OP 父头）/ LedgerMethods.h:204/332（账本）/ Ledger.cpp:1339（asyncGetBlockHeader）
  4. 与 FISCO 前例对拍：Ledger.cpp:234 写 + BaselineScheduler.h:259 + LedgerMethods.h:204 读（FISCO 是 pushView+mergeBackStorage 原子 mergeView，EngineServiceImpl.h:662-669）
- **产出**：核实报告（确认「mergeBackStorage 未调 → 不落盘」缺口）+ **范围决策**（是否修复——最小 loop 当前是「未接真实节点」的受控欠账，修复（接 mergeBackStorage）可能超收尾范围）
- **验证**：链路各段代码核对 + 缺口确认

### T3 — #5-C1 搬运已批准修复 + 修注释（⚠️ W8 审查 R2/R4 修正：非从零调查）
- **背景**：上游 `feat-op-validator-loop` 已拍板「Plan A：修共享」并落地（`a37517327`/`245f47f0c`/`4e0848e2f`，2026-07-31）——长字节/长列表长度前缀加 `if (lenOfLen>=2 && from[0]==0) return NonCanonicalSize`；本分支 `RLPDecode.h` 缺该修复（只有 `<56` 与单字节 0x81-0x00 检查，漏 `>=56` 的前导零）
- **动作**：① 确认上游修复内容（git show a37517327）；② **搬运进本分支 `RLPDecode.h`** + 移植 `RLPTest` 用例；③ **修注释失实**（`OpSchedulerImpl.h:277-278` 声称"shared decoder C1 fixed"实际未修）；④ 影响面核对（共享解码器消费方：Web3Transaction.cpp/ledger MPT/TransactionImpl/Web3AccessListResolver/ChecksumAddress——回归验证不破坏）；⑤ **用户拍板**（确认搬运已批准 Plan A vs 保持宽松——决策菜单两档，证据包含影响面/回归风险）
- **前导零定义**：长形式长度前缀首字节前导零（`0xb9 0x00 0xNN`），与标量 payload 前导零区分（后者 OP 字段层已拦）
- **产出**：搬运修复 + 注释修正 + 影响面回归 + 拍板记录
- **验证**：RLPTest 新用例绿 + 消费方回归 + 注释修正

### T4 — #5-C2/C6 核对 + 补测试 + 裁定（⚠️ W8 审查 R3/R4 修正：代码大多已实现）
- **C2 yParity**（⚠️ R3/R4：代码已实现，只补测试）：
  - typed-tx `signatureV ≤ 1` 校验**已存在**（Web3TxHandler.cpp:420/596/923，fb6f4aaa7）——**补测试**：(a) typed-tx yParity>1 → InvalidVInSignature 负向；(b) v=0/1 合法样本显式断言（现仅隐式覆盖）
  - **C2 位宽 = 7702 auth yParity 编码宽度（uint8/单字节）**（OpSchedulerImpl.h:374 `decodeAuthYParityScalar`，已实现）——补 7702 auth 宽度测试（超宽编码 0x82 0x01 0x00==256 应拒绝）
- **C6 硬拒定性**（⚠️ R3 修正：应裁定「放行/等价」非「硬拒」）：
  - legacy/0x01 在 FISCO OP 路径**走 generic 通道被执行**（decodeOneRawTx 分派 + opValidate 白名单 + processOpBlock generic）；op-geth **块执行层也放行**（state_transition.go IsDepositTx-only，拒绝在 txpool/排序器层）
  - **裁定**：legacy/0x01 块执行放行 = 与 op-geth 等价 → 不落地硬拒，**留档说明**（若硬拒会制造 FISCO 单侧分叉，与差分对拍目标冲突）
- **产出**：3 类测试 + C6 裁定留档
- **验证**：新测试绿 + 全量回归

### T5 — #5-语料可复现性验证（⚠️ W8 审查 R3 修正：重生成无意义）
- **背景**：currentRandom/currentCoinbase 恒值是生成器**结构性保证**（main.go:629-631 断言 mixDigest==0 + cases.go:312 全 case 同 coinbase）——重生成产出字节等同，**不可能多样化这些字段**；数量 **34 非 35**；校验机制是 `manifest.txt + regen.sh git-diff`（**无 SHA256SUMS**）
- **方法**：跑 `regen.sh`（W6 的 generator + op-geth v1.101702.2 pin e8800cff）——**可复现性验证**（确认当前入库语料确由 pinned op-geth 生成，regen 字节等同 exit 0）
- **产出**：可复现性验证报告（regen 跑通 + 字节等同确认）
- **验证**：regen.sh exit 0（git-diff 无差异）+ 既有 golden 消费正常

## 5. 流程

```
T1 ctest → T2 落盘 → T3 C1 搬运+拍板 → T4 C2/C6 核对+测试 → T5 语料可复现性验证
```

⚠️ W8 审查（R4）：**T4/T5 与 T3（C1）正交**——T3 拍板不阻塞 T4/T5。执行顺序：T1/T2 先行（独立验证）→ **T3 调查产证据包 + 用户拍板作为 checkpoint**（T4/T5 可在等待期先行或并行）→ 全部完成后 T3 落地（若拍板为搬运）+ 回归。

## 6. 验收标准

- #2：当前分支全量 ctest 复测通过（1909 全绿或明确失败清单修复；记忆基准只做比例参考）
- #4：s_number_2_header 落盘核实——确认「mergeBackStorage 未调 → 不落盘」缺口 + 范围决策（是否修复）
- #5-C1：搬运已批准修复（或拍板保持宽松）+ 注释失实修正 + 影响面回归 + 拍板记录
- #5-C2：3 类测试绿（typed-tx yParity>1 负向 / v=0/1 显式 / 7702 auth 宽度）
- #5-C6：裁定「放行/等价」留档（非硬拒）
- #5-语料：可复现性验证（regen.sh exit 0 + 字节等同）
- 全量回归不破

## 7. 不在 W8 范围

- **对比主线**（W1-W7 已全交付）
- **W7 交付项**（Karst 适配 / OP 回执修复 / PBFT 决策 / 生产互通——另立）
- **W0**（DU 冲突清理——独立收尾项）
- 合流策略（v1/v2 分支——独立决策，择期另立）
- ⚠️ W8 审查（R4）补充 parked 项（确认仍待办，不在本次）：
  - **批8**（接缝 11 条/嵌套 requires 软失败——op-executor-core-recovery 的批 7/8/5 之 8）
  - **非 20 字节 `/apps/` 表名状态根语义**（毒旗生产触发点，op-node 实连墙之一——op-validator-loop 四项决策之另一项，W8 未覆盖）
