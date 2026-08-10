# OP 回执/交易可查修复（方案 B）Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 OP 块的 `eth_getTransactionReceipt`/`eth_getTransactionByHash` 从「恒 null」变为「可查且带 op-geth 同款 OP 扩展字段」——OP 交易转换后写 `SYS_HASH_2_TX`，复用普通交易查询通道。

**Architecture:** 方案 B 三件事：① `EngineServiceImpl.cpp` 新增 `detail::opEnvelopeToTars` helper（OP 信封 → Web3Transaction → tars Transaction，填 extraTransactionHash + sender，失败返回 nullopt 不 throw），`registerOpBlock` 用它写 `SYS_HASH_2_TX` 替代 `s_eth_hash_2_rawtx`；② `combineReceiptResponse` 增加 opStackMeta 13 字段 → MarshalReceipt 形状输出；③ 写侧/读侧测试落地。engine→rpc PRIVATE 链接，`EngineServiceImpl.h` 只前向声明。

**Tech Stack:** C++20, tars-protocol, bcos-rpc (Web3Transaction), bcos-codec (RLP), Boost.Test, op-geth v1.101702.2 (MarshalReceipt 参照)

## Global Constraints

- Target/suite 名：`test-bcos-rpc`、`bcos-evm-opstack-tests`（非 bcos-rpc-ut / bcos-evm-opstack-ut）
- 写侧 helper `opEnvelopeToTars` 放 `EngineServiceImpl.cpp`，`EngineServiceImpl.h` **只前向声明**（D6）——`EngineServiceImpl.h` 不得 include `Web3Transaction.h`（避免 json/rpc Common/RPC_LOG 污染消费方）
- engine CMake 用 **PRIVATE** 链接 rpc（D5）：`target_link_libraries(engine PRIVATE rpc)`
- 写链**不 throw**（D7）：`opEnvelopeToTars` 失败返回 nullopt，`registerOpBlock` 跳过写表；0x04 (EIP-7702) 无 handler，不加 SetCode（§7）
- 非 deposit 必须填 `tarsTx.sender`（D8）——读侧 `from` 读 `tx.sender()`，`takeToTarsTransaction` 留空
- 必填 `extraTransactionHash` = keccak(完整信封)（D4）——不填读侧 `tx.hash()` 抛 `EmptyTransactionHash`
- `s_eth_hash_2_rawtx` 写**删除**（D1），不保留
- 读侧 `combineTxResponse` **不改**（extraTransactionBytes → decodeFromPayload 闭环已无损）
- 回归 suite：`test-bcos-rpc`（Web3ResponseTest/Web3EthMethodsTest/Web3RpcTest/Web3TypeTest）+ `bcos-evm-opstack-tests`（OpNewPayloadRpcE2eTest/OpEngineBranchSmokeTest/OpL1EdgeGateTest/GoldenSampleTest）
- 文档漂移同步：`DIVERGENCES.md:106`、comparison doc `:144/:416` 的 rawtx 写行为描述

---

### Task 1: 写侧 helper `opEnvelopeToTars`

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.cpp`（新增 helper + include）
- Modify: `engine/bcos-engine/EngineServiceImpl.h:93-157`（detail 命名空间加前向声明）
- Modify: `engine/CMakeLists.txt:14`（PRIVATE rpc 链接）

**Interfaces:**
- Consumes: `Web3Transaction::decode`（`bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp:57`）、`takeToTarsTransaction`（`:112`）、`sender()`（`:207`）、`codec::rlp::decode`
- Produces: `std::optional<bcostars::Transaction> detail::opEnvelopeToTars(bcos::bytes const& env, bcos::crypto::HashType const& txHash)` —— 后续 task 2 调用

- [ ] **Step 1: `EngineServiceImpl.cpp` 加 include**

在 `EngineServiceImpl.cpp` 的 include 区（`#include "EngineServiceImpl.h"` 之后）加：
```cpp
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <optional>
```

- [ ] **Step 2: `EngineServiceImpl.h` 加前向声明**

在 `namespace bcos::engine` 的 `namespace detail`（`:93`）内、`}  // namespace detail`（`:157`）之前加：
```cpp
/// OP 信封 → tars Transaction 转换（实现见 EngineServiceImpl.cpp；失败返回 nullopt，不 throw）。
/// 填 extraTransactionHash（D4）与 sender（D8）。0x04 等未知类型返回 nullopt（D7）。
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash);
```
（`bcostars::Transaction` 前向声明：`EngineServiceImpl.h` 顶部加 `namespace bcostars { struct Transaction; }`）

- [ ] **Step 3: `EngineServiceImpl.cpp` 实现 helper**

在 `EngineServiceImpl.cpp` 的 `namespace`（`:23` 匿名命名空间）之前加：
```cpp
namespace bcos::engine::detail
{
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash)
{
    bcos::rpc::Web3Transaction web3Tx;
    bcos::bytesRef envRef{const_cast<bcos::byte*>(env.data()), env.size()};
    if (auto err = bcos::codec::rlp::decode(envRef, web3Tx); err)
    {
        return std::nullopt;  // 未知类型（0x04）或损坏信封——不 throw（D7）
    }
    auto tarsTx = web3Tx.takeToTarsTransaction();
    // D4: 读侧 tx.hash() 返回 extraTransactionHash；不填则抛 EmptyTransactionHash
    tarsTx.extraTransactionHash.assign(txHash.begin(), txHash.end());
    // D8: 非 deposit 补 sender（takeToTarsTransaction 留空；读侧 from 读 tx.sender()）
    if (tarsTx.sender.empty())
    {
        auto sender = web3Tx.sender();  // ecrecover（Web3Transaction.cpp:207-223）
        tarsTx.sender.assign(sender.begin(), sender.end());
    }
    return tarsTx;
}
}  // namespace bcos::engine::detail
```

- [ ] **Step 4: `engine/CMakeLists.txt` 加 PRIVATE rpc 链接**

在 `engine/CMakeLists.txt:14` 的 `target_link_libraries(engine PUBLIC ...)` 行后加：
```cmake
target_link_libraries(engine PRIVATE rpc)
```

- [ ] **Step 5: 编译验证**

Run: `cmake --build build --target engine -j 8`
Expected: 编译通过（无 rpc 依赖错误、无 include 缺失）

- [ ] **Step 6: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.cpp engine/bcos-engine/EngineServiceImpl.h engine/CMakeLists.txt
git commit --no-verify -m "feat(engine): opEnvelopeToTars helper — OP 信封→tars Transaction（D4 hash/D7 兜底/D8 sender）+ PRIVATE rpc 链接"
```

---

### Task 2: `registerOpBlock` 写 `SYS_HASH_2_TX`

**Files:**
- Modify: `engine/bcos-engine/EngineServiceImpl.h:1299-1304`（写表逻辑替换）

**Interfaces:**
- Consumes: Task 1 的 `detail::opEnvelopeToTars`
- Produces: OP 交易落 `SYS_HASH_2_TX`（tars 编码），不再写 `s_eth_hash_2_rawtx`

- [ ] **Step 1: 替换写表代码**

`EngineServiceImpl.h:1299-1304` 当前：
```cpp
storage::Entry rawTxEntry;
rawTxEntry.set(rawTransactions[index]);
co_await storage2::writeOne(view,
    StateKey{SchedulerType::c_ethRawTxTable, toView(txHash)},   // s_eth_hash_2_rawtx
    std::move(rawTxEntry));
```
改为：
```cpp
// 方案 B：OP 交易转换后写 SYS_HASH_2_TX（复用普通交易通道）。原始信封转换失败
// （0x04 未知类型等）则跳过写表——读侧不可查但块仍有效（D7）。
if (auto tarsTx = detail::opEnvelopeToTars(rawTransactions[index], txHash))
{
    bcostars::protocol::TransactionImpl txImpl(
        [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
    bcos::bytes encodedTx;
    txImpl.encode(encodedTx);
    storage::Entry txEntry;
    txEntry.set(std::move(encodedTx));
    co_await storage2::writeOne(view,
        executor_v1::StateKey{ledger::SYS_HASH_2_TX,
            bcos::concepts::bytebuffer::toView(txHash)},
        std::move(txEntry));
}
```
（`EngineServiceImpl.h` 需 include `bcos-tars-protocol/protocol/TransactionImpl.h` 或确认已通过其他头引入；`ledger::SYS_HASH_2_TX` 与现有 `ledger::SYS_HASH_2_RECEIPT`（`:1289`）同源）

- [ ] **Step 2: 编译验证**

Run: `cmake --build build --target engine bcos-evm-opstack-tests -j 8`
Expected: 编译通过

- [ ] **Step 3: 运行 OP 写侧回归**

Run: `./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpNewPayloadRpcE2eSuite --log_level=test_suite`
Expected: 36 向量全 PASS（写表路径不破坏块执行；`SYS_HASH_2_TX` 写入不 throw）

- [ ] **Step 4: Commit**

```bash
git add engine/bcos-engine/EngineServiceImpl.h
git commit --no-verify -m "feat(engine): registerOpBlock 写 SYS_HASH_2_TX（opEnvelopeToTars）替代 s_eth_hash_2_rawtx"
```

---

### Task 3: 写侧落表测试（`OpNewPayloadRpcE2eTest`）

**Files:**
- Modify: `bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp`

**Interfaces:**
- Consumes: Task 2 的写表行为（`SYS_HASH_2_TX[txHash]` present + `c_ethRawTxTable` absent）
- Produces: 锁 D4（`tx.hash()==txHash`）与表切换的回归护栏

- [ ] **Step 1: 找 `runGoldenVector` 的 VALID assert 位置**

读 `OpNewPayloadRpcE2eTest.cpp`，定位 `PayloadValidationStatus::Valid` assert（约 `:225-228`）和 fixture 存储访问方式（`registerVerifiedBlock` `:165-175` 展示的 `fixture->multiLayerStorage` + `readOne` 模式，`GoldenSample.h:129` 的 `rawTransactions`）。

- [ ] **Step 2: 在 VALID assert 后加写侧落表断言**

在 7 字段断言后加：
```cpp
// 方案 B 写侧：OP 交易落 SYS_HASH_2_TX（tars 编码），s_eth_hash_2_rawtx 不再写。
auto const& rawTxs = *payload.rawTransactions;
auto& hashImpl = *fixture->cryptoSuite->hashImpl();
for (std::size_t i = 0; i < rawTxs.size(); ++i)
{
    auto txHash = hashImpl.hash(rawTxs[i]);
    // SYS_HASH_2_TX present + round-trip
    auto txEntry = readOne(*fixture->multiLayerStorage,
        StateKey{ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)});
    BOOST_REQUIRE(txEntry.has_value());
    auto tx = fixture->blockFactory->transactionFactory()->createTransaction(
        txEntry->get(), /*checkSig=*/false, /*checkHash=*/false, /*tainted=*/false);
    BOOST_CHECK_EQUAL(tx->hash(), txHash);  // 锁 D4 extraTransactionHash
    // rawtx 表 absent（D1：不保留）
    auto rawEntry = readOne(*fixture->multiLayerStorage,
        StateKey{SchedulerType::c_ethRawTxTable, bcos::concepts::bytebuffer::toView(txHash)});
    BOOST_CHECK(!rawEntry.has_value());
}
```
（`readOne`/`SchedulerType::c_ethRawTxTable`/`ledger::SYS_HASH_2_TX` 按该文件既有模式；若 `readOne` 是协程需 `co_await` 或 `syncWait`——对照该文件现有用法）

- [ ] **Step 3: 运行测试确认新断言绿**

Run: `cmake --build build --target bcos-evm-opstack-tests -j 8 && ./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpNewPayloadRpcE2eSuite`
Expected: 全 PASS（含新落表断言；`jovian_deposit_only` 向量顺带覆盖 deposit 写侧）

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/opstack/OpNewPayloadRpcE2eTest.cpp
git commit --no-verify -m "test(opstack): 写侧落表断言 — SYS_HASH_2_TX present + round-trip + rawtx absent"
```

---

### Task 4: 读侧回执 OP 字段（`combineReceiptResponse`）

**Files:**
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp`
- Modify: `bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.h`（如需要）

**Interfaces:**
- Consumes: `receipt.opStackMeta()`（`TransactionReceiptImpl.cpp:226`，空返回 nullopt）、`op-geth MarshalReceipt` 形状（`api.go:1779-1814`）
- Produces: 回执 JSON 的 OP 扩展字段（l1GasPrice/l1Fee/operatorFeeScalar…）

- [ ] **Step 1: 写 OP 字段输出测试（先红）**

`bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp` 加用例（按该文件现有 fixture/构建 receipt 的方式；用 `setOpStackMeta` 设 typed `OpStackReceiptMeta`）：
```cpp
BOOST_AUTO_TEST_CASE(combineReceiptResponseEmitsOpExtensionFieldsFromMeta)
{
    // 构造 receipt，设 opStackMeta（l1GasPrice=0x5、l1Fee=0xa、operatorFeeScalar=0x2）
    protocol::OpStackReceiptMeta meta;
    meta.l1_gas_price = bcos::u256(5);
    meta.l1_fee = bcos::u256(10);
    meta.operator_fee_scalar = 2;
    auto receipt = makeReceipt();
    receipt->setOpStackMeta(std::move(meta));
    auto tx = makeWeb3Tx();
    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *receipt, *tx, blockHash);
    BOOST_CHECK_EQUAL(result["l1GasPrice"].asString(), "0x5");
    BOOST_CHECK_EQUAL(result["l1Fee"].asString(), "0xa");
    BOOST_CHECK_EQUAL(result["operatorFeeScalar"].asString(), "0x2");
    // 空 meta 无 OP 字段
}
BOOST_AUTO_TEST_CASE(combineReceiptResponseOmitsOpFieldsWhenMetaEmpty)
{
    auto receipt = makeReceipt();  // 无 opStackMeta
    auto tx = makeWeb3Tx();
    Json::Value result = Json::objectValue;
    combineReceiptResponse(result, *receipt, *tx, blockHash);
    BOOST_CHECK(!result.isMember("l1GasPrice"));
    BOOST_CHECK(!result.isMember("l1Fee"));
}
```
Run: `cmake --build build --target test-bcos-rpc -j 8 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Response`
Expected: FAIL（`l1GasPrice` 未输出）

- [ ] **Step 2: 实现 OP 字段输出**

`ReceiptResponse.cpp` 的 `combineReceiptResponse` 末尾（`result["type"]` 之后）加：
```cpp
// OP 扩展字段（对照 op-geth MarshalReceipt api.go:1779-1814）。空 opStackMeta → 无输出。
if (auto meta = receipt.opStackMeta())
{
    if (meta->l1_gas_price)  result["l1GasPrice"] = toQuantity(*meta->l1_gas_price);
    if (meta->l1_gas_used)   result["l1GasUsed"]  = toQuantity(*meta->l1_gas_used);
    if (meta->l1_fee)        result["l1Fee"]      = toQuantity(*meta->l1_fee);
    if (meta->l1_blob_base_fee) result["l1BlobBaseFee"] = toQuantity(*meta->l1_blob_base_fee);
    if (meta->l1_base_fee_scalar) result["l1BaseFeeScalar"] = toQuantity(*meta->l1_base_fee_scalar);
    if (meta->l1_blob_base_fee_scalar) result["l1BlobBaseFeeScalar"] = toQuantity(*meta->l1_blob_base_fee_scalar);
    if (meta->operator_fee_scalar) result["operatorFeeScalar"] = toQuantity(*meta->operator_fee_scalar);
    if (meta->operator_fee_constant) result["operatorFeeConstant"] = toQuantity(*meta->operator_fee_constant);
    if (meta->da_footprint_gas_scalar) result["daFootprintGasScalar"] = toQuantity(*meta->da_footprint_gas_scalar);
    if (meta->da_footprint) result["blobGasUsed"] = toQuantity(*meta->da_footprint);  // Jovian 复用
    if (meta->deposit_nonce) result["depositNonce"] = toQuantity(*meta->deposit_nonce);
    if (meta->deposit_receipt_version) result["depositReceiptVersion"] = toQuantity(*meta->deposit_receipt_version);
    if (meta->operator_fee) result["operatorFee"] = toQuantity(*meta->operator_fee);  // FISCO 扩展
}
```
（`opStackMeta()` 返回 `std::optional<OpStackReceiptMeta>`，字段是 `std::optional<u256>`/`std::optional<uint64_t>`；`toQuantity` 对两者都适用——确认 `toQuantity(u256)`/`toQuantity(uint64_t)` 重载存在）

- [ ] **Step 3: 运行测试确认绿**

Run: `cmake --build build --target test-bcos-rpc -j 8 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Response`
Expected: PASS（两个新用例绿）

- [ ] **Step 4: 补读侧交易测试（spec §5-3/4/5）**

`Web3ResponseTest.cpp` 加：
```cpp
// deposit 查询：takeToTarsTransaction(deposit) → SYS_HASH_2_TX → createTransaction 读回 → combineTxResponse 最小输出
BOOST_AUTO_TEST_CASE(combineTxResponseDepositMinimalFields)
{
    auto web3Deposit = makeDepositTx();  // TransactionType::Deposit, from/sourceHash/mint 已设
    auto tarsTx = web3Deposit.takeToTarsTransaction();
    bcostars::protocol::TransactionImpl txImpl([tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
    Json::Value result = Json::objectValue;
    combineTxResponse(result, txImpl, txIndex, blockNumber, blockHash);
    BOOST_CHECK(result.isMember("nonce"));   // deposit nonce=0
    BOOST_CHECK_EQUAL(result["type"].asString(), "0x7e");
    // 无 accessList/blob 字段
    BOOST_CHECK(!result.isMember("accessList"));
    BOOST_CHECK(!result.isMember("blobVersionedHashes"));
}
// 4844 blob 查询：blobVersionedHashes 分支
BOOST_AUTO_TEST_CASE(combineTxResponseBlob4844)
{
    auto web3Tx = makeBlob4844Tx();  // type=EIP4844, blobVersionedHashes 已设
    auto tarsTx = web3Tx.takeToTarsTransaction();
    bcostars::protocol::TransactionImpl txImpl([tarsTx = std::move(tarsTx)]() mutable { return &tarsTx; });
    Json::Value result = Json::objectValue;
    combineTxResponse(result, txImpl, txIndex, blockNumber, blockHash);
    BOOST_CHECK(result.isMember("blobVersionedHashes"));
    BOOST_CHECK(result.isMember("maxFeePerBlobGas"));
}
```
Run: `cmake --build build --target test-bcos-rpc -j 8 && ./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Response`
Expected: PASS（deposit/4844 用例绿；deposit `r/s/v` 空行为在 `combineTxResponse:104-106` 对空 signatureData 已处理）

- [ ] **Step 5: 补 happy-path 回归（spec §5-5）**

`Web3EthMethodsTest.cpp` 的 RPCFixture 加 seeded-storage 成功用例——在现有 unknown-hash 负向用例（`:81-93`）旁，构造一个含 `SYS_HASH_2_TX` + `SYS_HASH_2_RECEIPT` 数据的 fixture，调 `eth_getTransactionReceipt`/`eth_getTransactionByHash`，断言返回非 null。按该文件现有 RPCFixture 的 seed 方式（对照 `Web3EthMethodsTest.cpp` 的 fixture 构造 + 既有 `makeReceipt`/`makeTx` 帮助函数）。
Expected: PASS（happy-path 返回完整对象）

- [ ] **Step 6: Commit**

```bash
git add bcos-rpc/bcos-rpc/web3jsonrpc/model/ReceiptResponse.cpp bcos-rpc/test/unittests/rpc/Web3ResponseTest.cpp bcos-rpc/test/unittests/rpc/Web3EthMethodsTest.cpp
git commit --no-verify -m "feat(rpc): combineReceiptResponse 输出 opStackMeta OP 字段 + deposit/4844/happy-path 读侧测试"
```

---

### Task 5: 文档同步 + 全量回归

**Files:**
- Modify: `bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md:106`
- Modify: `docs/opstack-opgeth-e2e-comparison.md:144/:416`

**Interfaces:**
- Consumes: Task 1-4 全部交付
- Produces: 方案 B 落定后文档一致 + 全量回归通过

- [ ] **Step 1: 更新 DIVERGENCES.md**

`:106` 附近记录 rawtx 写行为——改为「OP 交易转换后写 `SYS_HASH_2_TX`（方案 B）」，并加 0x04 (EIP-7702) 读侧 divergence 注记：
```markdown
- **OP 交易落库（方案 B，2026-08-10）**：registerOpBlock 转换后写 SYS_HASH_2_TX（tars 编码，key=keccak 信封 hash），不写 s_eth_hash_2_rawtx。读侧 eth_getTransactionByHash/eth_getTransactionReceipt 可查。
- **EIP-7702 (0x04) 读侧 divergence**：TransactionType 无 0x04 handler，0x04 交易跳过 SYS_HASH_2_TX 写表 → 读侧返回 null（块执行不受影响）。与 op-geth 的 divergence。
```

- [ ] **Step 2: 更新 comparison doc**

`:144`/`:416` 的 rawtx 描述改为「OP 交易转换后写 SYS_HASH_2_TX（方案 B，2026-08-10）」，并更新 §8.4 gap 表「OP 块回执不可查」状态（若实施完成标记修复）。

- [ ] **Step 3: 全量回归**

Run:
```bash
cmake --build build --target test-bcos-rpc bcos-evm-opstack-tests -j 8
./build/bcos-rpc/test/test-bcos-rpc --run_test=testWeb3Response,testWeb3Type,testWeb3EthMethods
./build/bcos-evm/test/bcos-evm-opstack-tests --run_test=OpNewPayloadRpcE2eSuite,OpL1EdgeGateSuite
```
Expected: 全 PASS（普通交易/回执回归不破）

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md docs/opstack-opgeth-e2e-comparison.md
git commit --no-verify -m "docs: 方案 B 落定 — OP 交易落 SYS_HASH_2_TX + 0x04 divergence 注记"
```
