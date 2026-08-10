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
| **D2** | **#5 裁定边界**：W8 用代码证据裁定 C1/C2/C6 + 语料重生成；**C1 修法先探明再定**（调查证据后用户拍板） | 用户选定 |
| **D3** | C1 流程：先调查（共享 RLPDecode.h 前导零现状 + OP 路径触发点）→ 证据拿全 → 用户拍板修法 | 用户选定 |
| **D4** | 语料重生成用 W6 的 op-geth v1.101702.2 环境（已解锁） | W6 已建环境 |

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
- **对象**：engine / rpc / ledger / tars-protocol / executor 模块（v2 重植相关）
- **方法**：全量 ctest 回归，对照历史基准（如 release-3.18 全量 1857/1858 的参考）
- **产出**：回归报告（全绿 / 失败清单 + 修复）
- **验证**：相关 target 全绿

### T2 — #4 s_number_2_header 落盘核实
- **对象**：`SYS_NUMBER_2_BLOCK_HEADER` 表（LedgerTypeDef.h）的 OP header 落盘链路
- **方法**：追「OP 密封 header → registerOpBlock → 落库 s_number_2_header → 读取路径」全链
- **产出**：核实报告（路径确认 / 若不一致则修复）
- **验证**：链路代码逐段核对 + 必要时测试

### T3 — #5-C1 调查（→ 用户拍板）
- **调查 1**：共享 `RLPDecode.h`（bcos-codec/rlp/RLPDecode.h）对长度前缀前导零的实际处理——现有 NonCanonicalSize 检查覆盖哪些、漏哪些
- **调查 2**：OP 路径（tx/header RLP 解码）哪里会遇前导零——W6 已证 OP golden 是规范 RLP，实际触发点在哪
- **产出**：证据包（共享解码器现状 + OP 触发点）→ **呈现给用户拍板**（修共享 vs OP 加层 vs 无需改）
- **验证**：证据包完整 + 用户拍板记录

### T4 — #5-C2/C6 裁定 + 修复
- **C2 yParity**：typed tx（2930/1559/4844）handler 加 `signatureV ≤ 1` 校验（typed-tx 记忆 bug 2）+ 测试（v=0/1 合法样本）
- **C6 硬拒定性**：legacy/0x01 在 OP 路径的处置（拒 vs 放行）——代码证据定性 + 落地
- **产出**：裁定 + 修复 + 测试
- **验证**：新测试绿 + 全量回归

### T5 — #5-语料重生成
- **对象**：`bcos-evm/test/opstack/t8n/vectors/`（currentRandom/currentCoinbase 全 35 份恒定）
- **方法**：用 W6 的 t8n generator + op-geth v1.101702.2 环境重生成
- **产出**：新语料 + SHA256SUMS 刷新 + 回归验证
- **验证**：SHA256SUMS 全 OK + 既有 golden 消费正常

## 5. 流程

```
T1 ctest → T2 落盘 → T3 C1 调查（→ 用户拍板）→ T4 C2/C6 裁定 → T5 语料重生成
```

每 task 独立可审；T3 需用户介入（拍板后继续）。

## 6. 验收标准

- #2：相关模块全量 ctest 复测通过（失败项修复）
- #4：s_number_2_header 落盘路径核实确认（或修复一致）
- #5-C1：证据包呈现 + 用户拍板 + 落地
- #5-C2：yParity 校验 + 测试绿
- #5-C6：硬拒定性 + 落地
- #5-语料：重生成 + SHA256SUMS 全 OK + 回归
- 全量回归不破

## 7. 不在 W8 范围

- **对比主线**（W1-W7 已全交付）
- **W7 交付项**（Karst 适配 / OP 回执修复 / PBFT 决策 / 生产互通——另立）
- **W0**（DU 冲突清理——独立收尾项）
- 合流策略（v1/v2 分支——独立决策，非本次）
