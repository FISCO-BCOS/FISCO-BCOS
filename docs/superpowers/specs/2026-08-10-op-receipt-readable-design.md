# OP 回执/交易可查修复（方案 B）设计

> 目标：让 OP 块的 `eth_getTransactionReceipt` / `eth_getTransactionByHash` 从「恒返回 null」变为「可查且带 op-geth 同款 OP 扩展字段」，通过**复用 `SYS_HASH_2_TX` 通用通道**（方案 B）。
> 状态：**Approved**。日期：2026-08-10。
> 关联：记忆 `op-gettransactionreceipt-parity`、`op-receipt-extended-fields-delivered`；comparison doc §8.4「OP 块回执不可查 🔴」。分支：`feat-op-executor-e2e`。

---

## 1. 背景与动机

W7 上线闸（comparison doc §8.4）判 No-Go 的阻塞项之一是 **OP 块回执不可查 🔴**：

- `EthEndpoint::getTransactionReceipt`（`EthEndpoint.cpp:777-807`）查询链：`getReceipt(SYS_HASH_2_RECEIPT)` ✅ 有 → `getTransactions(SYS_HASH_2_TX)` ❌ 空 → 抛 InvalidParams → **返回 null**。
- 根因：`registerOpBlock`（`EngineServiceImpl.h:1191-1306`）**刻意不写 `SYS_HASH_2_TX`**（OP 交易是 EIP-2718 信封，tars 解码器会静默吞错产假交易），交易本体只落 `s_eth_hash_2_rawtx`。

**方案 A（val-loop 已实现）**：读侧加 `s_eth_hash_2_rawtx` 回退 + RLP 反解码。写侧已有，读侧需移植。

**方案 B（本设计）**：OP 交易**转换后写 `SYS_HASH_2_TX`**（复用普通交易通道），查询链自动复活；不保留 `s_eth_hash_2_rawtx`。**用户拍板方案 B**（2026-08-10）。

## 2. 决策记录

| # | 决策 | 依据 |
|---|---|---|
| **D1** | **方案 B**：OP 交易转换后写 `SYS_HASH_2_TX`，不保留 `s_eth_hash_2_rawtx` | 用户拍板。查询链自动复活，省读侧回退 |
| **D2** | 转换在**引擎层 `registerOpBlock` 内联**，复用 `Web3Transaction`（`decode` + `takeToTarsTransaction`） | 用户拍板。零新增转换代码 |
| **D3** | 接受 **txpool 误 import 风险**：OP 交易进 `SYS_HASH_2_TX` 后，`asyncVerifyBlock`（`TxPool.cpp:261`）可能把它们当普通交易 fetch/import | 用户接受。风险触发依赖「PBFT 是否跑 asyncVerifyBlock」未决决策；最小 loop 未接真实节点，现无法验证 |
| **D4** | `takeToTarsTransaction` 转换后**必须手动填 `extraTransactionHash`** = keccak(完整信封) | 读侧 `tx.hash()`（`TransactionImpl.h:80-85`）返回 `extraTransactionHash`；不填则抛 `EmptyTransactionHash` |
| **D5** | 引擎层新增 `bcos-rpc` CMake 依赖（engine→rpc，单向无循环） | bcos-rpc 不依赖 engine（已核实 `bcos-rpc/CMakeLists.txt:34`） |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `engine/bcos-engine/EngineServiceImpl.h` | `registerOpBlock`：OP 信封 → `Web3Transaction::decode` → `takeToTarsTransaction` → 填 `extraTransactionHash` → 写 `SYS_HASH_2_TX`（替换 `s_eth_hash_2_rawtx`） |
| `engine/CMakeLists.txt` | 加 `bcos-rpc` 依赖 |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp` | `combineReceiptResponse` 增加 opStackMeta 13 字段 → MarshalReceipt 形状输出 |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.h` | （如需要）声明变更 |
| 测试 | `registerOpBlock` 落库测试 + `combineReceiptResponse` OP 字段断言 |

## 4. 关键机制

### 4.1 写侧（`registerOpBlock`，`EngineServiceImpl.h:1191`）

当前（`EngineServiceImpl.h:1299-1304`）：
```cpp
storage::Entry rawTxEntry;
rawTxEntry.set(rawTransactions[index]);   // 原始 EIP-2718 信封
co_await storage2::writeOne(view,
    StateKey{SchedulerType::c_ethRawTxTable, toView(txHash)},   // s_eth_hash_2_rawtx
    std::move(rawTxEntry));
```

改为：
```cpp
// 转换：原始信封 → Web3Transaction → tars Transaction
Web3Transaction web3Tx;
bcos::bytesRef envRef{...rawTransactions[index]...};
if (auto err = codec::rlp::decode(envRef, web3Tx); err) { throw ... }  // 含 deposit（DepositTxHandler）
auto tarsTx = web3Tx.takeToTarsTransaction();
// D4: 填 extraTransactionHash = keccak(完整信封)（读侧 tx.hash() 依赖）
tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());   // txHash = hashImpl.hash(原始信封)
// 编码成 tars 字节写 SYS_HASH_2_TX
TransactionImpl txImpl([tarsTx=std::move(tarsTx)]() mutable { return &tarsTx; });
bcos::bytes encodedTx; txImpl.encode(encodedTx);
storage::Entry txEntry; txEntry.set(std::move(encodedTx));
co_await storage2::writeOne(view,
    StateKey{ledger::SYS_HASH_2_TX, toView(txHash)},
    std::move(txEntry));
```

**关键细节**：
- 转换**二次解码**（registerOpBlock 只有原始字节，拿不到执行层内部的 `OpBlockTx`）——可接受，D2 已确认形态 1。
- deposit（0x7e）：`Web3Transaction::decode` 走 `DepositTxHandler`（`Web3TxHandler.cpp:611`）可解码；`takeToTarsTransaction` deposit 分支（`Web3Transaction.cpp:114`）填 sourceHash/mint/isSystemTransaction + 完整 0x7E 信封到 `extraTransactionBytes`。
- `takeToTarsTransaction` 不填 `extraTransactionHash` → D4 手动填。
- 读侧 `combineTxResponse`（`TransactionResponse.cpp:59-102`）从 `extraTransactionBytes` → `decodeFromPayload` 还原 Web3Transaction → 全字段输出（accessList/maxFeePerGas/4844 blob 均无损）。**无需改**。
- **deposit 读回**：deposit 的 `extraTransactionBytes` 是**完整 0x7E 信封**（`Web3Transaction.cpp:128` `encode()`），读侧 `decodeFromPayload`（`Web3Transaction.cpp:266-269` = `decode(in, false)`）经 `DepositTxHandler::decode`（`Web3TxHandler.cpp:664`）能处理完整信封 → 还原为 `Web3Transaction{type=Deposit}`，`combineTxResponse` 输出最小字段（nonce/type/value，走 `:63-102` 的 Web3 分支）。deposit 无签名，`signatureData` 为空 → `r/s/v` 输出空（`:104-106`），可接受（pre-existing，§7）。

### 4.2 读侧回执 OP 字段（`combineReceiptResponse`，`ReceiptResponse.cpp:10`）

当前只输出基础字段（status/from/to/logs/type 等），**无 opStackMeta 映射**。补：

对照 op-geth `MarshalReceipt`（`internal/ethapi/api.go:1779-1814`）与 opStackMeta 13 字段（`TransactionReceipt.tars:22-36`）：

| op-geth 输出 | 门控 | opStackMeta 字段 |
|---|---|---|
| `l1GasPrice` | `IsOptimism && !IsDepositTx` | `l1_gas_price` |
| `l1GasUsed` | 恒出（Fjord 后废弃不输出，但字段在） | `l1_gas_used` |
| `l1Fee` | 恒出 | `l1_fee` |
| `l1BlobBaseFee` | Ecotone+ | `l1_blob_base_fee` |
| `l1BaseFeeScalar` | Ecotone+ | `l1_base_fee_scalar` |
| `l1BlobBaseFeeScalar` | Ecotone+ | `l1_blob_base_fee_scalar` |
| `operatorFeeScalar` | Isthmus+ | `operator_fee_scalar` |
| `operatorFeeConstant` | Isthmus+ | `operator_fee_constant` |
| `daFootprintGasScalar` | Jovian+ | `da_footprint_gas_scalar` |
| `blobGasUsed` | Jovian+（复用为 DA footprint） | `da_footprint` |
| `depositNonce` | `IsDepositTx && nonce != nil` | `deposit_nonce` |
| `depositReceiptVersion` | deposit | `deposit_receipt_version` |
| `operatorFee` | FISCO 扩展（op-geth 不输出） | `operator_fee` |

**注意**：`l1FeeScalar`（Ecotone 前 legacy 标量）opStackMeta 无字段——但 FISCO 对拍基线 Ecotone+，实际无缺口。

**输出形状**：opStackMeta 字段是 hex string（0 值存 `"0x0"`），对照 op-geth 输出：
- 数值字段（l1GasPrice/l1Fee/l1BlobBaseFee）：直接 hex 输出
- 标量字段（scalar/constant）：hexutil.Uint64 → hex 输出
- deposit 字段：仅 deposit 交易输出
- 空 opStackMeta（普通回执）：不输出任何 OP 字段（保持现状）

## 5. 验证

- **写侧**：OP 块执行后，`SYS_HASH_2_TX` 有交易（tars 编码），`s_eth_hash_2_rawtx` 不再写
- **读侧交易**：`getTransactionByHash(OP tx)` 返回完整交易（from/to/gas/input/r/s/v/type/accessList…），deposit 返回最小对象
- **读侧回执**：`getTransactionReceipt(OP tx)` 返回基础字段 + OP 扩展字段（l1GasPrice/l1Fee/operatorFeeScalar… 对照 MarshalReceipt）
- **回归**：普通块交易/回执查询不受影响；`combineReceiptResponse` 空 opStackMeta 不输出 OP 字段
- **全量 ctest**：rpc 相关 suite 全绿

## 6. 验收标准

- OP 交易写 `SYS_HASH_2_TX`（tars 编码），不写 `s_eth_hash_2_rawtx`
- `getTransactionByHash`/`getTransactionReceipt` 对 OP 交易不再返回 null
- 回执输出 OP 扩展字段（对齐 MarshalReceipt）
- 普通交易/回执查询回归不破
- 引擎层 `bcos-rpc` 依赖构建通过

## 7. 不在本 spec 范围

- **txpool 误 import 风险**：接受（D3），不做防御——触发依赖 PBFT 未决决策，另行处理
- **`eth_getRawTransactionByHash` RPC**：需原始字节，方案 B 已丢，不做
- **PBFT 共识层决策**（asyncVerifyBlock 是否活跃）：独立未决项
- **contractAddress 缺失**（evmone 无 created_address）：pre-existing，不入
- **deposit from/to 空**（Web3Transaction 不支持 0x7e sender 恢复）：pre-existing，接受
