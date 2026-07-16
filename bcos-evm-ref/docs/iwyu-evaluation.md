# IWYU 评估报告 — `bcos-evm-ref`（含 mapping）

日期：2026-07-10  
工具：`include-what-you-use 0.26`（Homebrew clang 22.1.8）  
编译数据库：`bcos-evm-ref/build/compile_commands.json`（31 TU）  
Mapping：`bcos-evm-ref/iwyu-bcos-evm-ref.imp`  
脚本：`bcos-evm-ref/scripts/iwyu-check.sh`

---

## 1. Mapping 文件引入

### 文件

`bcos-evm-ref/iwyu-bcos-evm-ref.imp`（纯 JSON 数组，**不可写 `//` 注释**）

### 编码的项目约定

| 规则 | 说明 |
|------|------|
| `evmc/evmc.h` → `evmc/evmc.hpp` | 统一 C++ 包装头 |
| `evmc/bytes.hpp` → `evmc/evmc.hpp` | bytes32/address 经 evmc.hpp 伞形提供 |
| `test/state/*.hpp` 细粒度头 → `test/state/state.hpp` | evmone state 伞形头策略 |

### 未纳入 mapping 的约定（及原因）

| 约定 | 原因 |
|------|------|
| `<stdint.h>` → `<cstdint>` 等 C/C++ 头互换 | 与 IWYU 内置 STL 映射**冲突**（`Same file seen with two different visibilities: <stdint.h>` assertion） |
| `intx` 细粒度拆分禁止 | 项目已统一 `<intx/intx.hpp>`；IWYU 很少拆 intx |
| `test/utils/test_state.hpp` 伞形 | TestState 与 state.hpp 用途不同；tests 应显式 include harness 头 |

### 调用方式（macOS IWYU 0.26 必须 `-Xiwyu` 前缀）

```bash
bcos-evm-ref/scripts/iwyu-check.sh                    # 全量
bcos-evm-ref/scripts/iwyu-check.sh bcos-evm-ref/opstack/OpValidate.cpp  # 单文件
```

---

## 2. 量化对比（mapping 前 vs 后）

| 指标 | 无 mapping | 有 mapping | 变化 |
|------|-----------|-----------|------|
| 输出总行数 | 1126 | 903 | **-20%** |
| 有 `should-add` 的 TU | 35 | 32 | -3 |
| 有 `should-remove` 的 TU | 9 | 7 | -2 |
| 细粒度 `test/state/{transaction,block,account,state_view,state_diff}.hpp` 建议次数 | **52** | **0** | **消除** |
| 细粒度 `evmc/bytes.hpp` 建议次数 | 12 | 0（归入 evmc.hpp） | **消除** |
| `test/state/state.hpp` 伞形建议 | 12 | 13 | 略增（预期：合并替代拆分） |

**结论**：mapping **显著消除 evmone state 细粒度拆分噪声**，使建议与项目伞形头策略一致。

残余噪声主要来自：

- `<stdint.h>` / `<stddef.h>`（IWYU 默认 C 风格，无法安全 mapping）
- STL 细拆（`<string>`、`<map>`、`<optional>` 等 — 属 IWYU 默认行为，本项目可接受）
- 测试 TU 的大量「补全直接 include」（头瘦身后下游 TU 应显式 include — **正确信号**）

---

## 3. 生产代码可执行项（mapping 后）

### 3.1 已在上轮 IWYU 修复中完成 ✅

| 项 | 状态 |
|----|------|
| `OpFeeParams.h` 不再公开 include `<test/state/state_view.hpp>`（I-1） | ✅ `ae339ef06` |
| `OpTransition.cpp` 删除未用 `<evmone/constants.hpp>` | ✅ |
| `RollupCost.h` / `OpReceiptMeta.h` / `OpDepositTx.h` / `OpHost.h` 头瘦身 | ✅ |
| `OpHostTest.cpp` 删除未用 `OpForkSchedule.h` | ✅ |

### 3.2 mapping 后仍建议、但**不建议自动应用**的项

| 文件 | IWYU 建议 | 裁定 |
|------|----------|------|
| `OpDepositTx.h` | 删 `transaction.hpp` + fwd-decl，改 `#include <test/state/state.hpp>` | **拒绝**：与头瘦身/fwd-decl 策略冲突；按值成员 `TransactionReceipt` 只需 `transaction.hpp`，不必拉全 state 伞形 |
| `OpReceiptMeta.h` | 同上模式 | **拒绝**：同上，保持 `transaction.hpp` + fwd-decl `OpFeeParams`/`OpForkConfig` |
| `OpFeeParams.cpp` | 删 `state_view.hpp`，改 `state.hpp` | **可选**：`.cpp` 用最小头可接受；若追求与 mapping 一致可改 `state.hpp`（编译略重，行为不变） |
| `OpForkSchedule.h` | `evmc/evmc.h` → `evmc/evmc.hpp` | **可采纳**：`evmc_revision` 仍可用，一行低风险 |
| `RollupCost.h` | 删 `<evmc/bytes.hpp>` | **已处理**：当前 `<evmc/bytes.hpp>` 为 `bytes_view` 最小依赖，合理 |
| `OpTransition.cpp` | 删 `bloom_filter.hpp` / `hash_utils.hpp` | **需核实**：若符号仅经传递 include 获得可删；否则保留显式 include |

### 3.3 mapping 后零建议的生产 TU

**无**。所有生产 `.cpp` 仍有「补全 STL / stdint」类建议 — 属 IWYU 风格偏好，非缺陷。

---

## 4. 与头瘦身策略的关系（重要）

本轮 P1 后执行的 **头瘦身**（fwd-decl + include 下沉 `.cpp`）与 **mapping 伞形策略**在**公共头**上存在张力：

- **头瘦身目标**：公共 API 头最小依赖、加快增量编译
- **mapping 目标**：include 列表与 `test/state/state.hpp` 伞形一致

**项目裁定**（推荐长期策略）：

1. **公共头（`include/bcos-evm-ref/`）**：优先头瘦身；mapping 的「改 state.hpp」建议对公共头 **人工否决**
2. **实现文件（`.cpp`）**：可用 `state.hpp` 伞形（与 mapping 一致）或保持最小细粒度头（如 `state_view.hpp`）— 团队择一，文档化即可
3. **测试（`test/`）**：遵循「用到什么 include 什么」；mapping 后测试 TU 的 ADD 建议大多合理

---

## 5. 后续可选治理

| 优先级 | 动作 |
|--------|------|
| P2 | `OpForkSchedule.h`：`evmc/evmc.h` → `evmc/evmc.hpp`（一行，低风险） |
| P3 | CI 可选门：`scripts/iwyu-check.sh` + 解析器只报 `should-remove`（未使用 include） |
| defer | C 头 mapping：需 `--no_internal_mappings` + 自建完整 STL 映射，成本高 |
| defer | `EthTransition.h` IWYU：建议拆 `state.hpp` 为 fwd-decl（与现有伞形 include 冲突，收益小） |

---

## 6. 总结

- **Mapping 文件已引入**，成功将 evmone state / evmc 细粒度建议收敛为项目伞形头，输出噪声 **降低约 20%**，细粒度 state 建议 **从 52 次降至 0**。
- **不建议**按 mapping 全自动改公共头（会破坏已完成的头瘦身）。
- **生产代码 IWYU 健康度**：上轮 A+B+C 修复后，无未使用 include 残留；mapping 后 `should-remove` 仅 7 处，多为策略冲突而非垃圾头。
- 全量测试仍 **59/59 opstack PASS**（IWYU/mapping 为只读分析，未改行为）。
