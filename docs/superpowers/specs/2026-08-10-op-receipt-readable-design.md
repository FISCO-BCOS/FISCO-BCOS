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
| **D2** | 转换在**引擎层 `registerOpBlock` 触发**，复用 `Web3Transaction`（`decode` + `takeToTarsTransaction`） | 用户拍板。零新增转换代码 |
| **D3** | 接受 **txpool 误 import 风险**：OP 交易进 `SYS_HASH_2_TX` 后，`asyncVerifyBlock`（`TxPool.cpp:261`）可能把它们当普通交易 fetch/import | 用户接受。风险触发依赖「PBFT 是否跑 asyncVerifyBlock」未决决策；最小 loop 未接真实节点，现无法验证 |
| **D4** | `takeToTarsTransaction` 转换后**必须手动填 `extraTransactionHash`** = keccak(完整信封) | 读侧 `tx.hash()`（`TransactionImpl.h:80-85`）返回 `extraTransactionHash`；不填则抛 `EmptyTransactionHash` |
| **D5** | 引擎层新增 `bcos-rpc` CMake 依赖（**PRIVATE** 链接，engine→rpc 单向无循环） | bcos-rpc 不依赖 engine（已核实 `bcos-rpc/CMakeLists.txt:34`）；PRIVATE 避免 rpc 链接集传播到所有 engine 消费方 |
| **D6** | ⚠️ 审查③加固：转换抽成**非模板 helper** `opEnvelopeToTars` 放 `EngineServiceImpl.cpp`（唯一 include Web3Transaction.h 的 TU），`EngineServiceImpl.h` 只前向声明 | 避免 `EngineServiceImpl.h` 消费方（engine/test、libinitializer、bcos-evm opstack tests）解析 jsoncpp + rpc Common + `RPC_LOG` 宏污染 |
| **D7** | ⚠️ 审查①加固：写链**不 throw**——`opEnvelopeToTars` 失败（0x04 未知类型/损坏信封）返回 nullopt，`registerOpBlock` 跳过写表 | 0x04 无 handler，throw 会中止整个块注册（比现状存 raw 更糟） |
| **D8** | ⚠️ 审查①修复：非 deposit 转换后**填 `tarsTx.sender`**（从 `web3Tx.sender()` 恢复） | 读侧 `from` 读 `tx.sender()`；`takeToTarsTransaction` 留空 sender，OP 写链不经过 txpool 的 ecrecover |

## 3. 组件变更清单

| 文件 | 变更 |
|---|---|
| `engine/bcos-engine/EngineServiceImpl.cpp` | **新增 helper** `detail::opEnvelopeToTars(bytes, txHash) → std::optional<bcostars::Transaction>`：Web3Transaction decode + takeToTarsTransaction + 填 extraTransactionHash + 填 sender；失败返回 nullopt（D6/D7/D8） |
| `engine/bcos-engine/EngineServiceImpl.h` | `registerOpBlock`：调 `opEnvelopeToTars`（前向声明）→ 成功则 `TransactionImpl::encode` 写 `SYS_HASH_2_TX`（替换 `s_eth_hash_2_rawtx`）；失败跳过 |
| `engine/CMakeLists.txt` | 加 `target_link_libraries(engine PRIVATE rpc)`（D5） |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp` | `combineReceiptResponse` 增加 opStackMeta 13 字段 → MarshalReceipt 形状输出 |
| `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.h` | （如需要）声明变更 |
| 测试 | 写侧落表（`OpNewPayloadRpcE2eTest`）+ 读侧 OP 字段/deposit/4844/happy-path（`Web3ResponseTest`/`Web3EthMethodsTest`）——见 §5 |
| 文档 | `DIVERGENCES.md:106`、comparison doc `:144/:416` 的 rawtx 写行为描述同步更新（§7 文档漂移） |

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

改为（⚠️ 审查①修正：转换抽成**非模板 helper**，放 `EngineServiceImpl.cpp`，`EngineServiceImpl.h` 只前向声明——避免 json/rpc Common 拉进所有 include 者 + RPC_LOG 宏污染 + rpc 链接 PUBLIC 传播）：

```cpp
// EngineServiceImpl.h 只声明，实现放 EngineServiceImpl.cpp（私有 include Web3Transaction.h）
// bcos::engine::detail::opEnvelopeToTars(bytes const& env, HashType txHash)
//   -> bcostars::Transaction   // 失败返回 nullopt（如 0x04 未知类型），调用方决定存 raw 还是跳过
```

`EngineServiceImpl.cpp` 里的 helper 实现：
```cpp
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash)
{
    Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = codec::rlp::decode(envRef, web3Tx); err)
        return std::nullopt;   // 未知类型（如 0x04）或损坏信封——不 throw，避免中止块注册
    auto tarsTx = web3Tx.takeToTarsTransaction();
    // D4: extraTransactionHash = keccak(完整信封)（读侧 tx.hash() 依赖）
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    // 审查①修正：非 deposit 填 sender（takeToTarsTransaction 留空；读侧 from 读 tx.sender()）
    if (tarsTx.sender.empty())
        tarsTx.sender.assign(web3Tx.sender().begin(), web3Tx.sender().end());  // 或 ecrecover
    return tarsTx;
}
```

`registerOpBlock` 调用：
```cpp
if (auto tarsTx = detail::opEnvelopeToTars(rawTransactions[index], txHash))
{
    TransactionImpl txImpl([tarsTx=std::move(*tarsTx)]() mutable { return &tarsTx; });
    bcos::bytes encodedTx; txImpl.encode(encodedTx);
    storage::Entry txEntry; txEntry.set(std::move(encodedTx));
    co_await storage2::writeOne(view,
        StateKey{ledger::SYS_HASH_2_TX, toView(txHash)}, std::move(txEntry));
}
// 转换失败（0x04 等未知类型）：跳过写表（与现状存 raw 相比，读侧不可查但块仍有效）
```

**关键细节**：
- **转换抽 helper + PRIVATE rpc 链接**（审查③推荐）：`engine/CMakeLists.txt` 加 `target_link_libraries(engine PRIVATE rpc)`，helper 定义在 `EngineServiceImpl.cpp`（唯一 include Web3Transaction.h 的 TU）——避免 `EngineServiceImpl.h` 的消费方（engine/test、libinitializer、bcos-evm opstack tests）解析 jsoncpp + rpc Common + `RPC_LOG` 宏污染。
- 转换**二次解码**（registerOpBlock 只有原始字节，拿不到执行层内部的 `OpBlockTx`）——可接受，D2 已确认形态 1。
- **🔴 from 修复（审查① CRITICAL）**：`takeToTarsTransaction` 非 deposit 分支**留空 sender**（`Web3Transaction.cpp:190`「sender left empty — TxValidator::verify() computes them」）；读侧 `combineTxResponse:27`/`combineReceiptResponse:29` 的 `from` 读 `tx.sender()`。普通流程靠 txpool `TxValidator::verify` ecrecover 补 sender，**OP 写链不经过 txpool** → 必须 helper 里填 sender（从 `web3Tx.sender()` 恢复，`Web3Transaction.cpp:207-223` 已实现）。否则所有非 deposit OP 交易读侧 `from: "0x"`。
- **🔴 0x04 未处理（审查① REFUTED）**：`TransactionType` 枚举无 0x04（`Web3Transaction.h:39-46`），`handlerFor` 无 SetCode case → `decode` 返回 `UnsupportedTransactionType`。**不能 throw**（否则中止整个块注册，比现状存 raw 更糟）→ helper 返回 nullopt，registerOpBlock 跳过写表。spec 的「7702(0x04) 已处理」声明**删除**——W6 `isthmus_setcode_7702` 向量的 0x04 交易读侧将不可查（或按 §7 决策是否加 SetCode handler）。
- deposit（0x7e）：`decode` 走 `DepositTxHandler`（`Web3TxHandler.cpp:611`）可解码；`takeToTarsTransaction` deposit 分支（`Web3Transaction.cpp:114`）填 sourceHash/mint/isSystemTransaction + 完整 0x7E 信封 + **sender=from**（`:121`）→ 读侧 from 正常（deposit from 非空，§7 描述修正）。
- `takeToTarsTransaction` 不填 `extraTransactionHash` → D4 手动填。
- 读侧 `combineTxResponse`（`TransactionResponse.cpp:59-102`）从 `extraTransactionBytes` → `decodeFromPayload` 还原 Web3Transaction → 输出 nonce/value/accessList/maxFeePerGas/4844 blob（2930/1559/4844 无损）。**读侧交易输出无需改**（from 走 sender，写侧已填）。
- **deposit 读回**：deposit 的 `extraTransactionBytes` 是**完整 0x7E 信封**（`Web3Transaction.cpp:128` `encode()`），读侧 `decodeFromPayload`（`Web3Transaction.cpp:266-269` = `decode(in, false)`）经 `DepositTxHandler::decode`（`Web3TxHandler.cpp:664`）能处理 → 还原 `Web3Transaction{type=Deposit}`，`combineTxResponse` 输出最小字段。deposit 无签名 → `r/s/v` 输出空（`:104-106`），可接受（pre-existing）。
- **legacy InputTooLong（审查① minor）**：legacy EIP-155 的 `encodeForSign()` 含 `chainId,0,0` 尾，`decodeFromPayload` 留 2 个占位 → 返回 `InputTooLong`；`combineTxResponse:62` 丢弃该错误 → base 字段仍正确解码。功能正常（OP 交易是 EIP-155），记录不修。

### 4.2 读侧回执 OP 字段（`combineReceiptResponse`，`ReceiptResponse.cpp:10`）

当前只输出基础字段（status/from/to/logs/type 等），**无 opStackMeta 映射**。补：

对照 op-geth `MarshalReceipt`（`internal/ethapi/api.go:1779-1814`）与 opStackMeta 13 字段（`TransactionReceipt.tars:22-36`）：

| op-geth 输出 | 门控 | opStackMeta 字段 |
|---|---|---|
| `l1GasPrice` | `IsOptimism && !IsDepositTx` | `l1_gas_price` |
| `l1GasUsed` | 恒出（协议层 Fjord 起废弃，但 MarshalReceipt 仍输出） | `l1_gas_used` |
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

## 5. 验证（⚠️ 审查④修正：验证必须落地为测试，点名 harness）

**回归 suite**（点名，非「rpc 相关 suite」）：
- `test-bcos-rpc`：`Web3ResponseTest`（combine* 直接）+ `Web3EthMethodsTest`（EthEndpoint dispatch）+ `Web3RpcTest`（EIP1559/4844 send path）+ `Web3TypeTest`（decode/encode round-trip）
- `bcos-evm-opstack-tests`：`OpNewPayloadRpcE2eTest`（36 向量，写侧回归网）+ `OpEngineBranchSmokeTest` + `OpL1EdgeGateTest` + `GoldenSampleTest`

**新增测试**（审查④要求）：
1. **写侧落表**（`OpNewPayloadRpcE2eTest::runGoldenVector` 的 VALID assert 后）：`SYS_HASH_2_TX[txHash]` present + `createTransaction(bytes, false, false, false)` round-trip + `tx.hash() == txHash`（锁 D4）+ `c_ethRawTxTable[txHash]` absent。向量 `jovian_deposit_only` 顺带覆盖 deposit 写侧。
2. **读侧回执 OP 字段**（`Web3ResponseTest`）：set `opStackMeta` + 断言 MarshalReceipt 字段输出（l1GasPrice/l1Fee/operatorFeeScalar…）；显式「空 meta → 无 OP 字段」（`!result.isMember("l1GasPrice")`）。适配当前 `OpStackReceiptMeta`/`setOpStackMeta` API（val-loop 的 `8f3df2f40` 用例用 `OpReceiptMetaCodec`，本分支不存在，需改写）。
3. **deposit 查询**：`takeToTarsTransaction(deposit)` → 写 `SYS_HASH_2_TX` → `createTransaction` 读回 → `combineTxResponse` 最小输出（nonce/type/value，无 accessList/blob）+ `r/s/v` 空行为锁定。
4. **4844 blob 查询**：`combineTxResponse` 的 `blobVersionedHashes` 分支单测。
5. **happy-path 回归**：`Web3EthMethodsTest` RPCFixture 加 seeded-storage 的 `eth_getTransactionByHash`/`getTransactionReceipt` 成功用例（当前只有 unknown-hash 负向，`Web3EthMethodsTest:81-93`）。
6. **0x04 决策**：W6 `isthmus_setcode_7702` 向量下 0x04 交易读侧不可查（helper 返回 nullopt 跳过写表）——加测试锁定「块仍 VALID + 该交易查询返回 null」，或决策加 SetCode handler（本 spec 默认不加，§7）。

## 6. 验收标准

- OP 交易写 `SYS_HASH_2_TX`（tars 编码），不写 `s_eth_hash_2_rawtx`
- `getTransactionByHash`/`getTransactionReceipt` 对 OP 交易不再返回 null（deposit 返回最小对象；0x04 交易按 §5-6 决策返回 null）
- 非 deposit OP 交易读侧 `from` 正确（写侧已填 sender）
- 回执输出 OP 扩展字段（对齐 MarshalReceipt）
- 普通交易/回执查询回归不破（`test-bcos-rpc` + `bcos-evm-opstack-tests` 全绿）
- 引擎层 `bcos-rpc` 依赖构建通过（PRIVATE 链接，`EngineServiceImpl.h` 消费方不拉 json）

## 7. 不在本 spec 范围

- **txpool 误 import 风险**：接受（D3），不做防御——触发依赖 PBFT 未决决策，另行处理
- **`eth_getRawTransactionByHash` RPC**：需原始字节，方案 B 已丢，不做
- **PBFT 共识层决策**（asyncVerifyBlock 是否活跃）：独立未决项
- **contractAddress 缺失**（evmone 无 created_address）：pre-existing，不入
- **EIP-7702 (0x04) 读侧支持**：不加 SetCode handler（`TransactionType` 枚举无 0x04），0x04 交易跳过写表、读侧返回 null——与 op-geth 的 divergence 记入 DIVERGENCES（W6 `isthmus_setcode_7702` 向量）
- **legacy InputTooLong**：EIP-155 legacy 反解码恒返 `InputTooLong`（`chainId,0,0` 尾），`combineTxResponse:62` 丢弃错误，base 字段正常——记录不修
- **deposit from**：**非空**（`takeToTarsTransaction` deposit 分支填 `sender=from`，`Web3Transaction.cpp:121`）；仅 `to` 可能 null（contract-creation）
- **文档漂移**：`DIVERGENCES.md:106`、`docs/opstack-opgeth-e2e-comparison.md:144/:416` 记录了将被替换的 rawtx 写行为——实施时同步更新
- **deposit from/to 空**（Web3Transaction 不支持 0x7e sender 恢复）：pre-existing，接受
