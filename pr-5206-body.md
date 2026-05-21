## 摘要

本 PR 在 **evmone 0.21 Phase 2** 基础上继续交付 EVM 语义与 Web3 兼容性，并新增 **EIP-7702（type-4 `authorization_list`）** 全栈支持（**仅 `transaction-executor` / `executor.version == 1`**）。

**近期提交（7702，本分支待 push）：**
- `feat(evm): EIP-7702 authorization_list on transaction-executor path` — RPC/Tars/TxPool 解析与门控、v1 执行与 Compat 单测

**既有 Phase 2 提交：**
- `602022bc2` — `perf(bcos-executor)`: thread_local evmone VM
- `bf3701bae` — `feat(evm)`: Phase 2 主体（修订版、预编译、Compat 套件等）
- `8613ee1c9` — `feat(evm)`: EIP-2929/2930 transaction warm-up (W1 + W2)

---

## EIP-7702 变更要点

### 协议与 RPC
- `Web3Transaction`：type `0x04`、`AuthorizationListEntry`、RLP 编解码；**空 `authorization_list` 解码失败**
- `Transaction.tars`：`authorizationList` tag 16；`TransactionImpl::web3AuthorizationList()` 缓存
- `Web3Eip7702Fill` / `Web3Eip7702Apply`：extra bytes Path B/C、secp256k1 `0x05` 签名域恢复
- `Web3Eip2930Fill`：信封白名单增加 `0x04`

### 执行（仅 v1）
- `TransactionExecutorImpl`：`applyEip7702AuthorizationList`、`warmEip7702Addresses`、`m_eip7702Refund`（apply 在 savepoint 刷新前，revert 不撤销委托）
- `HostContext`：内在 gas `25000×N`、委托 CALL/DELEGATECALL、EXTCODE 返回 23 字节指示符

### 门控
- `EthEndpoint` + `TxValidator::validateEip7702Admission`：`executor.version == 0` 或 SM2 链拒绝 type-4

### 测试
- `bcos-rpc`：`Web3Eip7702TransactionTest`（RLP round-trip、空列表拒绝、Tars 桥接）、`EthEndpointTest`（legacy/SM2 拒绝）
- `bcos-tars-protocol`：`TransactionImplEip7702Test`（缓存引用、`AnyHolder` 尺寸）
- `bcos-executor`：`Web3Eip7702FillTest`、`CompatStaticGuards`（`FC_G_7702_*`）
- `transaction-executor`：`CompatEip7702ExecuteContext`、`CompatHostContext/Eip7702`、`CompatEip7702Smoke`

---

## 明确未包含 / 已知限制

| 项 | 说明 |
|----|------|
| **Legacy executor** | `bcos-executor` / `TransactionExecutive` **不**处理 `authorization_list`；type-4 在 v0 路径拒绝 |
| **EIP-3529 refund cap** | M1 仅累加 `gas_refund`，不实施 `refund <= gasUsed/5`（`RefundCapDeferred` 文档化） |
| **EIP-7691 Blob** | PBFT 无 blob 交易（N/A） |
| **PoS system_call** | 不调用 `system_call_block_start/end` |
| **EIP-6780** | selfdestruct 返回 true，无同 tx 创建合约追踪 |
| **EIP-2929 I1** | warm 集不跨 nested CALL（独立跟踪项） |
| **官方向量目录** | `bcos-rpc/test/data/eip7702/` 待钉版本（可选后续） |

---

## 测试计划

- [ ] `./test-bcos-rpc --run_test=testWeb3Eip7702`
- [ ] `./test-bcos-rpc --run_test=testEthEndpoint`
- [ ] `./test-bcos-tars-protocol --run_test=TransactionImplEip7702`
- [ ] `./test-bcos-executor --run_test=Web3Eip7702Fill`
- [ ] `./test-bcos-executor '--run_test=Compat/CompatStaticGuards'`
- [ ] `./test-transaction-executor --run_test=CompatEip7702ExecuteContext`
- [ ] `./test-transaction-executor --run_test=CompatHostContext/Eip7702`
- [ ] `./test-transaction-executor --run_test=CompatEip7702Smoke`
- [ ] `./test-bcos-executor '--run_test=Compat*'`（发版前建议全量 Compat）

---

## 相关文档（仓库内）

- `docs/superpowers/specs/2026-05-21-eip7702-fullstack-design.md`
- `docs/superpowers/plans/2026-05-21-eip7702-implementation-plan.md`
- `docs/superpowers/plans/2026-05-18-evmone-phase2-complete.md`
