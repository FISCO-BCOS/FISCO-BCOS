# OP Stack 交叉验证交接文档

**日期**: 2026-08-19
**目的**: 交由另一个 AI 独立验证规范 vs 实现对比结论

## 任务概述

独立验证 FISCO BCOS 对 OP Stack 执行层客户端规范的实现完整度。

## 交叉验证要求

### 1. 独立提取需求 (不看现有 YAML)

从以下来源独立提取原子需求项，与现有 102 条对比：

- OP Stack 规范: https://specs.optimism.io/
- op-geth 源码: github.com/ethereum-optimism/op-geth (参考实现)
- OP Stack 技术规格: https://raw.githubusercontent.com/ethereum-optimism/develop/specs/rollup-node.md

### 2. 独立映射代码

对每条需求，在以下代码路径中定位实现：

```
engine/bcos-engine/EngineServiceImpl.h    # Engine API
opstack-executor/OpstackExecutor.h        # 交易解码 + execute
opstack-executor/OpBlockExecute.h         # 块级操作
bcos-evm/bcos-evm/opstack/OpTransition.cpp # transition + fee
bcos-evm/bcos-evm/opstack/OpFeeParams.h   # fee 参数
bcos-evm/bcos-evm/opstack/OpPredeploys.h  # 预部署地址
bcos-evm/bcos-evm/opstack/OpForkSchedule.h # 分叉调度
```

### 3. 验证判定标准

| 状态 | 判定条件 |
|------|----------|
| IMPLEMENTED | 代码存在 + 对应测试通过 |
| PARTIAL | 代码存在但有已知限制/桩实现 |
| MISSING | 代码不存在或功能未激活 |
| NOT_APPLICABLE | 规范要求不适用于本实现 |

### 4. 输出格式

独立产出一份对比报告，至少包含：
- 需求项清单（与现有 YAML 的 diff）
- 代码映射结果
- 差距分类
- 对现有结论的认同/异议

## 现有产出物

| 文件 | 说明 |
|------|------|
| `op-spec-requirements.yaml` | 现有 102 条需求（供对比，非权威） |
| `op-code-mapping.yaml` | 现有代码映射（供对比，非权威） |
| `op-spec-gap-report.md` | 现有差距报告（供对比，非权威） |

## 关键验证点

现有结论中最需要交叉验证的：

1. **Receipt 13 字段全部 IMPLEMENTED** — 第一次分析曾判定为 GAP
2. **V4 端点是桩** — 未验证 V4 端点的实际行为
3. **-38006 错误码缺失** — 未对照 op-geth 的错误码完整清单
4. **辅助预部署 9 项 MISSING** — 需确认哪些是共识必要的

## 反向验证

交叉验证应产出：
- ✅ 现有结论正确（认同）
- ❌ 现有结论有误（异议 + 理由）
- ➕ 现有遗漏（新发现的需求项）
- ➖ 现有多余（不成立的需求项）
