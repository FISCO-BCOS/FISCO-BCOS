# OP Stack 规范 vs FISCO 实现 Gap Report

**日期**: 2026-08-19
**方法论**: [spec-vs-code-comparison-procedure.md](superpowers/specs/2026-08-19-spec-vs-code-comparison-procedure.md)
**输入**: [op-spec-requirements.yaml](op-spec-requirements.yaml) + [op-code-mapping.yaml](op-code-mapping.yaml)

## 总体评估

| 指标 | 值 | 比例 |
|------|----|------|
| 总需求项 | 102 | 100% |
| ✅ 已实现 | 91 | 89.2% |
| ⚠️ 部分实现 | 1 | 1.0% |
| ❌ 缺失 | 10 | 9.8% |
| N/A | 0 | 0% |

**结论**: 核心执行路径完全对齐。缺失项集中在 V4 端点(桩)和辅助预部署合约(非阻塞)。

## 优先级矩阵

| 优先级 | 数量 | 状态 |
|--------|------|------|
| 🔴 P0 (MUST+MISSING) | 0 | 无阻塞项 |
| 🟡 P1 (SHOULD+MISSING/PARTIAL) | 2 | 需排期 |
| 🟢 P2 (MAY+MISSING) | 8 | 可延后 |

## 按扇区分析

### 1. Engine API (ENG) — 37 项

| 状态 | 数量 | 详情 |
|------|------|------|
| ✅ IMPLEMENTED | 35 | V3 端点完整实现 |
| ⚠️ PARTIAL | 1 | V4 能力广告 |
| ❌ MISSING | 1 | -38006 TooDeepReorg |

**亮点**:
- `exchangeCapabilities`: OP 模式完整能力列表
- `handleOpNewPayload`: 分类屏障(catch-rewrap)
- `buildOpPayload`: 属性驱动 OP 构建
- `calcOpBaseFee`: Holocene/Jovian EIP-1559

**差距**:

#### 🟡 ENG-CAP-002: V4 能力广告
- **状态**: PARTIAL
- **严重度**: SHOULD
- **描述**: V4 端点已注册但是桩实现
- **影响**: 真实 op-node 连接需 V4
- **建议**: 实现 V4 解析层或降级广告

#### 🟡 ENG-FCU-007: -38006 TooDeepReorg
- **状态**: MISSING
- **严重度**: SHOULD
- **描述**: reorg 深度超限时返回 -38006
- **影响**: 与 op-node 错误处理不兼容
- **建议**: 在 updateForkchoice 中添加深度检查

### 2. 块执行 (BLK) — 20 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 20 |

**全部实现**。关键验证点:
- pre-block: system_call_block_start + attribute 解析
- deposit 0x7E: 15+ 拒绝分支(严格 RLP)
- tx: chain_id 校验 + 白名单类型 + balance cap
- seal: 6 承诺字段 + Jovian DA footprint
- commit: 无 block reward + Prague requests 抑制

### 3. 预部署合约 (PRE) — 16 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 15 |
| ❌ MISSING | 1 |

**已实现**: L1Block/GPO/SequencerFeeVault/BaseFeeVault/L1FeeVault/OperatorFeeVault/MessagePasser + 存储槽映射 + system_call

**差距**:

#### 🟢 PRE-ADR-008: WETH 合约
- **状态**: MISSING
- **严重度**: MAY
- **描述**: WETH9 预部署合约未部署
- **影响**: 不影响共识,仅影响 dApp 兼容性
- **建议**: 后续按需部署

### 4. 费用计算 (FEE) — 24 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 24 |

**全部实现**。关键链路:
- L1 data fee: Ecotone(calldata) → Fjord(FastLZ)
- Operator fee: Isthmus/Jovian ×100
- DA footprint: Jovian blob gas used
- EIP-1559: Holocene/Jovian extraData + minBaseFee

### 5. Receipt 编码 (RCP) — 13 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 13 |

**全部实现**。关键点:
- 0x7E deposit: [status, cumGas, bloom, logs, depositNonce, receiptVersion]
- typed tx: prefix || rlp([status, cumGas, bloom, logs])
- OpReceiptMeta: 13 个 OP 扩展字段
- trie root: op-geth Receipts.EncodeIndex 语义

### 6. 状态管理 (STA) — 5 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 5 |

**全部实现**:
- stateRootOf: visitAccounts → rlp → computeTrieRoot
- computeOpTxRoot: key=rlp(index), leaf=raw-envelope
- opStorageRoot: secure-trie, key=keccak256(slot)
- applyStateDiff: diff → state 更新
- Storage2State: SharedErrorSlot 错误通道

### 7. 硬分叉 (FRK) — 8 项

| 状态 | 数量 |
|------|------|
| ✅ IMPLEMENTED | 8 |

**全部实现**:
- Ecotone → EVMC_CANCUN
- Fjord → EVMC_CANCUN
- Granite → EVMC_CANCUN
- Holocene → EVMC_CANCUN
- Isthmus → EVMC_PRAGUE
- Jovian → EVMC_PRAGUE
- Karst → 占位别名

## 实施建议

### 短期 (可选)

1. **ENG-FCU-007**: 实现 -38006 TooDeepReorg
   - 估计: 0.5 天
   - 位置: `EngineServiceImpl.h` updateForkchoice

2. **ENG-CAP-002**: V4 端点桩→真
   - 估计: 2-3 天
   - 位置: `EngineEndpoint.cpp` + `EngineServiceImpl.h`

### 中期 (按需)

3. **PRE-ADR-008**: WETH 部署
   - 估计: 0.5 天
   - 位置: `OpPredeploys.h` + genesis 配置

### 无需操作

- 91 项已实现需求: 保持现状
- 辅助预部署(CrossDomainMessenger 等): 用户已裁定不处理

## 方法论复用

此对比方法论可复用于:
1. **新版本规范对比**: 更新 op-spec-requirements.yaml,重新映射
2. **其他执行层对比**: 替换扇区定义,复用三层架构
3. **回归测试**: 每次重大变更后重新运行对比

## 附录: 产出物清单

| 文件 | 描述 |
|------|------|
| `docs/op-spec-requirements.yaml` | 102 条原子需求 |
| `docs/op-code-mapping.yaml` | 代码映射(YAML) |
| `docs/op-spec-gap-report.md` | 本报告 |
| `docs/superpowers/specs/2026-08-19-spec-vs-code-comparison-procedure.md` | 方法论规范 |
| `docs/superpowers/specs/2026-08-19-op-spec-comparison-methodology.md` | 方法论设计稿 |
