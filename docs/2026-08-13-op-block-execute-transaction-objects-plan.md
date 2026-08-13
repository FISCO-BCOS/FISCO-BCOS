# OP 块执行消费 Transaction 对象 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** route B 的 execute hook 直接消费块的 `Transaction` 对象，消除普通 tx 的 raw-envelope 解析与 tars 重转；`decodeOneRawTx` 及 4 个 typed/legacy decoder 删除，`OpTxDecode.h` 删除；三项共识检查（chain-id / EIP-2 low-s / yParity>1）搬进 `OpstackExecutor::m_prepare`。

**Architecture:** execute hook 按类型字节分类（0x7e → `decodeDepositTx` 收集 `deposits`；0x01/0x02/0x04/legacy → 直接用块里的 `transactions`；其它字节 → `OpConsensusError`）；`runOpBlockInjection` 签名从 `txs(OpBlockTx)+normalTxs` 改为 `transactions(块的)+deposits+rawTxBytes`，循环内按类型字节分派；三项共识检查新增 `validateEnvelopeSignature`（OpRlpDecode.h）并在 `m_prepare`（opValidate 之前，`call==false` 才强制）调用。canonical 往返 + 定宽检查放弃，由六路承诺比较兜底。`OpBlockTx`/`processOpBlock` 保留（route-A 测试参照）。

**Tech Stack:** C++20, bcos-codec RLP, evmone (evmc/intx/evmmax), bcos-task coroutines, Boost.Test。

## Global Constraints

- 库纯净性：`engine` 不得依赖 bcos-evm（三项检查不能放 `opEnvelopeToTars`，见设计 §5）。
- 共识不变：低-s / chain-id / yParity 拒绝必须保持（六路承诺比较不兜底这三项）。
- `OpBlockTx`/`processOpBlock`/`validateJovianBlockShape(span<OpBlockTx>)` **保留不动**（route-A 测试参照，无生产调用者）。
- 引擎侧 `buildOpBlock`/`opEnvelopeToTars` 不动；`executeDeposit` 仍吃 `DepositTx`。
- 双路径等价测试 DIVERGE=0、语料（131 向量）必须保持绿。
- eth_call 语义不变（`call==true` 跳过三项检查）。
- 提交前 `git add` 已格式化文件；commit 被 clang-format hook 拒时用 `--no-verify`（仅 .md 提交场景）。

---

### Task 1: `validateEnvelopeSignature`（OpRlpDecode.h）+ 单元测试

**Files:**
- Modify: `opstack-executor/OpRlpDecode.h`（`bcos::evm::engine::detail`，`recoverTxSender` 之后追加）
- Test: `opstack-executor/tests/OpRlpDecodeTest.cpp`

**Interfaces:**
- Consumes: 已有 RLP 基元 `enterList`/`expectExhausted`/`decodeU64Scalar`/`decodeU256Scalar`/`decodeOptionalAddressField`/`decodeBytesField`/`decodeAccessList`/`decodeAuthorizationList`/`narrowGasLimit`/`requireLowSSignature`（本文件）。
- Produces: `void validateEnvelopeSignature(bcos::bytes const& rawEntry, uint64_t chainId)` — 链 id 匹配 / EIP-2 low-s / yParity>1 违规抛 `OpConsensusError`；deposit（0x7e）无签名直接返回；不 ecrecover、不 canonical 往返。Task 2 的 `m_prepare` 消费。

- [ ] **Step 1: 写失败测试**（OpRlpDecodeTest.cpp 追加 `BOOST_AUTO_TEST_CASE(ValidateEnvelopeSignature)`）

测试信封全部用 `encodeTuple` 手工构造（不需真签名——本函数只查结构 + 签名值，不 ecrecover）。`using bcos::evm::eth::detail::encodeTuple;`（include `<bcos-evm/eth/RlpEncodeTuple.h>`）。

```cpp
// 0x02 envelope: [chainId, nonce, maxPriority, maxFee, gas, to, value, data, accessList, yParity, r, s]
bcos::bytes makeEip1559(uint64_t chainId, intx::uint256 yParity, intx::uint256 r, intx::uint256 s)
{
    auto body = encodeTuple(chainId, uint64_t{0}, intx::uint256{0}, intx::uint256{0},
        uint64_t{21000}, evmc::bytes_view{}, intx::uint256{0}, evmc::bytes_view{},
        evmone::state::AccessList{}, yParity, r, s);
    bcos::bytes out{0x02};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
// legacy envelope: [nonce, gasPrice, gas, to, value, data, v, r, s]
bcos::bytes makeLegacy(intx::uint256 v)
{
    auto body = encodeTuple(uint64_t{0}, intx::uint256{1}, uint64_t{21000}, evmc::bytes_view{},
        intx::uint256{0}, evmc::bytes_view{}, v, intx::uint256{1}, intx::uint256{1});
    return {body.begin(), body.end()};
}

// 断言用例（kChainId = 0x2105）：
//  - 合法 eip1559（r=1,s=1 低-s）→ BOOST_CHECK_NO_THROW(validateEnvelopeSignature(makeEip1559(0x2105, 0, 1, 1), 0x2105))
//  - chain-id 不匹配 → BOOST_CHECK_THROW(..., OpConsensusError)
//  - yParity=2 → BOOST_CHECK_THROW(..., OpConsensusError)
//  - s > secp256k1n/2 → BOOST_CHECK_THROW(..., OpConsensusError)  // s = 0x8000...（33 字节左对齐 intx）
//  - 合法 legacy v=27 → no throw；v=0x2105*2+35+0 → no throw（chain 匹配）
//  - legacy v=0x2105*2+35+2（parity=2 → 派生 chainId 错）→ BOOST_CHECK_THROW(..., OpConsensusError)
//  - deposit 0x7e 信封 → no throw（无签名）
```

- [ ] **Step 2: 跑测试确认失败**

Run: `rtk cargo test -p bcos-opstack-executor OpRlpDecodeTest`（或仓库实际目标名，见 Task 5 的构建命令）
Expected: 编译失败 —— `validateEnvelopeSignature` 未声明。

- [ ] **Step 3: 实现 `validateEnvelopeSignature`**（OpRlpDecode.h，`recoverTxSender` 之后、关闭 namespace 前）

```cpp
/// Envelope-shape consensus checks relocated from the retired raw-tx decoders (OpTxDecode.h):
/// chain-id binding, EIP-2 low-s, yParity<=1. Runs before execution on the Transaction-object
/// path (OpstackExecutor::m_prepare). Validates only — never constructs a Transaction, no
/// ecrecover (the executor's opValidate recovers the sender itself). Dispatch mirrors the
/// retired decodeOneRawTx: deposit (0x7e) has no signature (decodeDepositTx owns its strictness)
/// and returns; typed 0x01/0x02/0x04 and legacy (>= 0xc0) are checked. Unknown type bytes are the
/// caller's concern (the execute hook rejects them before this is reached).
inline void validateEnvelopeSignature(bcos::bytes const& rawEntry, uint64_t chainId)
{
    if (rawEntry.empty())
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: empty envelope");
    constexpr uint8_t kRlpListBase = 0xc0;
    constexpr uint8_t kDepositTypeByte = 0x7e;
    const auto typeByte = rawEntry[0];
    if (typeByte >= kRlpListBase)
    {
        // legacy: [nonce, gasPrice, gasLimit, to, value, data, v, r, s]
        bcos::bytesRef body(rawEntry.data(), rawEntry.size());
        auto listBody = enterList(body);
        (void)decodeU64Scalar(listBody);            // nonce
        (void)decodeU256Scalar(listBody);           // gasPrice
        (void)narrowGasLimit(decodeU64Scalar(listBody), "legacy.gasLimit");
        (void)decodeOptionalAddressField(listBody); // to
        (void)decodeU256Scalar(listBody);           // value
        (void)decodeBytesField(listBody);           // data
        const auto v = decodeU256Scalar(listBody);
        const auto r = decodeU256Scalar(listBody);
        const auto s = decodeU256Scalar(listBody);
        expectExhausted(listBody, "legacy envelope fields");
        expectExhausted(body, "legacy envelope (trailing bytes after the field list)");
        if (v != 27 && v != 28)
        {
            // EIP-155: v = chainId*2 + 35 + parity. parity>1 pollutes the division -> derived
            // chainId mismatch, so the chain-id check also rejects invalid parity here.
            if (v < 35)
                throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid legacy v");
            if ((v - 35) / 2 != intx::uint256{chainId})
                throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch (legacy)");
        }
        requireLowSSignature(r, s);
        return;
    }
    if (typeByte == kDepositTypeByte)
        return;  // deposit: unsigned; decodeDepositTx owns its strictness
    // typed 0x01/0x02/0x04: [chainId, nonce, fees, gasLimit, to, value, data, accessList,
    // (0x04) authorizationList, yParity, r, s] — field order per rlp_encode.cpp.
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    const auto txChainId = decodeU64Scalar(listBody);
    if (txChainId != chainId)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: chain id mismatch");
    (void)decodeU64Scalar(listBody);  // nonce
    switch (typeByte)
    {
    case 0x01:  // access-list: single gasPrice
        (void)decodeU256Scalar(listBody);
        break;
    case 0x02:  // eip1559
    case 0x04:  // set-code
        (void)decodeU256Scalar(listBody);  // maxPriorityFeePerGas
        (void)decodeU256Scalar(listBody);  // maxFeePerGas
        break;
    default:
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: unsupported tx type byte");
    }
    (void)narrowGasLimit(decodeU64Scalar(listBody), "typed.gasLimit");
    (void)decodeOptionalAddressField(listBody);  // to
    (void)decodeU256Scalar(listBody);            // value
    (void)decodeBytesField(listBody);            // data
    (void)decodeAccessList(listBody);            // accessList
    if (typeByte == 0x04)
        (void)decodeAuthorizationList(listBody);  // authorizationList
    const auto yParity = decodeU256Scalar(listBody);
    if (yParity > 1)
        throw OpConsensusError("OpSchedulerImpl: raw tx decode: invalid y parity");
    const auto r = decodeU256Scalar(listBody);
    const auto s = decodeU256Scalar(listBody);
    expectExhausted(listBody, "typed envelope fields");
    expectExhausted(body, "typed envelope (trailing bytes after the field list)");
    requireLowSSignature(r, s);
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `rtk cargo test -p bcos-opstack-executor OpRlpDecodeTest`
Expected: 全 PASS（新用例 8 条）。

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/OpRlpDecode.h opstack-executor/tests/OpRlpDecodeTest.cpp
git commit -m "feat(opstack): validateEnvelopeSignature（chain-id/low-s/yParity 共识检查迁出 decode 层）"
```

---

### Task 2: 三项检查接入 `OpstackExecutor::m_prepare`

**Files:**
- Modify: `opstack-executor/OpstackExecutor.h`（`m_prepare` 签名 + 体内调用；`executeTransaction` 透传）
- Test: `opstack-executor/tests/OpstackExecutorTest.cpp`（追加回归用例）

**Interfaces:**
- Consumes: Task 1 的 `bcos::evm::engine::detail::validateEnvelopeSignature`。
- Produces: `m_prepare(storage, blockHeader, transaction, ledgerConfig, fee={}, blockGasLeft=0, chainId=0, call=false)` —— `call==false` 且 envelope 非空时先跑签名检查再 `opValidate`；`call==true`（eth_call）跳过，行为不变。

- [ ] **Step 1: 写失败测试**（OpstackExecutorTest.cpp 追加）

构造带 `extraTransactionBytes` = 跨链 0x02 信封的 tars Transaction（复用 Task 1 的 `makeEip1559` 模式，或引 corpus 的跨链向量）；用 OpstackExecutorTest 现有 fixture 的最小存储 + header：

```cpp
// 复用 fixture 的 storage/header；构造 tx：extraTransactionBytes = 跨链信封（chainId 0x2106）
auto receipt = co_await executor.executeTransaction(storage, header, *tx,
    /*contextID=*/0, ledgerConfig, /*call=*/false, fee, blockGasLeft, /*chainId=*/0x2105, &hashes);
// 期望：BOOST_CHECK_THROW(OpConsensusError) —— 链 id 不匹配在 opValidate 之前被拒
// call=true 时：BOOST_CHECK 抛出的不是 OpConsensusError（跳过检查，走 opValidate）
```

- [ ] **Step 2: 跑测试确认失败**

Run: `rtk cargo test -p bcos-opstack-executor OpstackExecutorTest`
Expected: 编译通过但用例失败 —— 跨链信封没被拒（m_prepare 未接检查）。

- [ ] **Step 3: 实现**（OpstackExecutor.h）

`m_prepare` 签名追加 `uint64_t chainId = 0, bool call = false`；在 `auto envRef = transaction.extraTransactionBytes();` 之后、`opValidate` 之前插入：

```cpp
if (!call && !envRef.empty())
    // envRef 是 bytesConstRef（Transaction.h:89），validateEnvelopeSignature 收 bcos::bytes——
    // 需 .toBytes() 拷贝（每 tx 一次，可接受）。gate !envRef.empty() 兜住空 envelope。
    detail::validateEnvelopeSignature(envRef.toBytes(), chainId);
```

`executeTransaction` 的 `m_prepare(...)` 调用改为：

```cpp
auto props = co_await m_prepare(storage, blockHeader, transaction, ledgerConfig, fee,
    blockGasLeft, chainId, call);
```

`ExecuteContext::prepare`（concept 路径，OP 不走）保持默认参数，不动。

- [ ] **Step 4: 同步修 `OpstackExecutorTest` 现有用例，再跑测试确认通过**

**关键前置**：`buildWeb3Tx`（OpstackExecutorTest.cpp:67-88）经 `takeToTarsTransaction` 把 `extraTransactionBytes` 写成 **signing preimage**（Web3Transaction.cpp:220-223，无 yParity/r/s、chainId=5），现有用例以 `call=false, chainId=10` 调 `executeTransaction`（:165/:241/:285/:456）——Task 2 后 `validateEnvelopeSignature` 把 preimage 当 legacy/截断 envelope 解析必抛 `OpConsensusError`，这些成功用例全变红。修复：`buildWeb3Tx` 构建 tars tx 后把 `extraTransactionBytes` **覆盖为完整 EIP-2718 envelope**（`web3Tx.encode()`，chainId 与 executeTransaction 调用一致），镜像 `buildOpBlock` 的 SEV-8 overwrite（EngineServiceImpl.h:1188）；若覆盖后 `opValidate` 的 flz 变化影响断言，同步调整。

Run: `rtk cargo test -p bcos-opstack-executor`
Expected: 新用例（跨链拒绝）PASS；现有用例经 Step 4 修复后全绿。

- [ ] **Step 5: 提交**

```bash
git add opstack-executor/OpstackExecutor.h opstack-executor/tests/OpstackExecutorTest.cpp
git commit -m "feat(opstack): 三项共识检查接入 m_prepare（call==false 强制，eth_call 跳过）"
```

---

### Task 3: 核心重构 —— `runOpBlockInjection` 新签名 + execute hook + 全部调用点

**Files:**
- Modify: `opstack-executor/OpBlockExecute.h`（`runOpBlockInjection` 签名+主体；空 envelope 守卫；删 `EnvelopeToTarsConverter` 别名）
- Modify: `opstack-executor/OpScheduler.h`（execute hook 重写；空 envelope 守卫；ctor/member 删 `m_envelopeToTars`；**`OpTxDecode.h` include 保留到 Task 4**）
- Modify: `opstack-executor/OpTxDecode.h`（`decodeDepositTx` 返回 `DepositTx`；`decodeOneRawTx` deposit 分支适配——本任务只改返回类型，不删其它 decoder）
- Modify: `libinitializer/Initializer.cpp:563-570`（OpScheduler ctor 去掉 converter lambda 参数）
- Modify: `opstack-executor/tests/OpBlockInjectorTest.cpp`（两个用例的新签名调用）
- Modify: `opstack-executor/tests/OpSchedulerTest.cpp`（`runRouteBDirect` 新形状；Fixture ctor :326-327 删 `makeConverter()`；删 `makeConverter`/`decodeOneRawTx` 用法）
- Modify: `opstack-executor/tests/OpDualPathEquivalenceTest.cpp`（route B 新形状；route-A ctor :1107-1113 删 lambda；`hasStorageScan` :1175 适配；删 `buildFiscoTx` + 孤立的 `evmoneTxFieldDiff`/`evmoneTxFieldsEqual`）
- Modify: `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp:213-218`（fixture 构造 OpScheduler 删 converter lambda 实参）

**Interfaces:**
- Consumes: Task 1 的 `validateEnvelopeSignature`（经 `m_prepare`）；`decodeDepositTx` → `DepositTx`。
- Produces:
  - `runOpBlockInjection(executor, view, header, transactions(span<Transaction::ConstPtr>), deposits(span<DepositTx>), cfg, chainId, ledgerConfig, rawTxBytes, hashImpl)` —— 循环按 `rawTxBytes[i][0]` 分派：0x7e → `executeDeposit(deposits[depIdx++])`；普通 → `executeTransaction(*transactions[i])`；未知字节 → `OpConsensusError`。`txTypes[i]` 从 `rawTxBytes[i][0]` 推（legacy→0）。
  - execute hook 不再调用 `decodeOneRawTx`；`OpScheduler` 不再持 `m_envelopeToTars`。

- [ ] **Step 1: 重写 `runOpBlockInjection`**（OpBlockExecute.h）

```cpp
template <class Storage>
OpExecuteBlockResult runOpBlockInjection(bcos::executor_v1::opstack::OpstackExecutor& executor,
    Storage& view, bcos::protocol::BlockHeader const& header,
    std::span<bcos::protocol::Transaction::ConstPtr const> transactions,
    std::span<bcos::evm::opstack::DepositTx const> deposits,
    bcos::evm::opstack::OpForkConfig const& cfg, uint64_t chainId,
    bcos::ledger::LedgerConfig const& ledgerConfig, std::vector<bcos::bytes> const& rawTxBytes,
    bcos::crypto::Hash::Ptr const& hashImpl)
{
    namespace detail = bcos::evm::engine::detail;
    namespace op = bcos::evm::opstack;
    namespace eth = bcos::executor_v1::eth;

    auto blk = detail::toBlockInfo(header);
    std::optional<std::string> hashErr;
    detail::RecentBlockHashes<Storage> hashes(
        view, blk.number, detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);
    eth::StorageStateView<Storage> stateView(view);

    // (1) Pre-block system call (unchanged).
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first content check + Jovian shape (type-byte classification, no raw-tx parse).
    constexpr uint8_t kDepositTypeByte = 0x7e;
    constexpr uint8_t kRlpListBase = 0xc0;
    if (rawTxBytes.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    if (rawTxBytes[0][0] != kDepositTypeByte || deposits.empty() ||
        !op::isL1AttributesTx(deposits[0]))
        throw OpConsensusError("op block: first tx is not the L1 attributes deposit");
    if (cfg.has_da_footprint)
    {
        auto const& data = deposits[0].data;
        if (data.size() == op::IsthmusL1AttributesLen)
        {
            if (rawTxBytes.back()[0] != kDepositTypeByte)
                throw OpConsensusError(
                    "op block: unexpected non-deposit transactions in Jovian activation block");
        }
        else
        {
            if (data.size() < op::JovianL1AttributesLen)
                throw OpConsensusError(
                    "op block: L1 attributes transaction data too short for DA footprint gas scalar");
            if (!std::equal(op::JovianL1AttributesSelector.begin(),
                    op::JovianL1AttributesSelector.end(), data.begin()))
                throw OpConsensusError(
                    "op block: L1 attributes transaction data does not have Jovian selector");
        }
    }

    op::OpBlockResult result;
    result.receipts.reserve(rawTxBytes.size());
    int64_t blockGasLeft = blk.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    std::size_t depIdx = 0;
    op::OpFeeParams fee{};
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        if (rawTxBytes[i].empty())  // 空 envelope：直接 raw[0] 会越界；旧 decodeOneRawTx 的干净 INVALID 保留
            throw OpConsensusError("op block: empty envelope");
        if (rawTxBytes[i][0] == kDepositTypeByte)
        {
            if (seenNonDeposit)
                throw OpConsensusError("op block: deposit after non-deposit tx");
            if (depIdx >= deposits.size())
                throw OpConsensusError("runOpBlockInjection: deposits list shorter than block");
            auto receipt = bcos::task::syncWait(executor.executeDeposit(
                view, header, deposits[depIdx++], chainId, blockGasLeft, ledgerConfig, &hashes));
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(op::kDepositTxType));
        }
        else
        {
            // normal: 0x01/0x02/0x04/legacy (>=0xc0); unknown typed byte -> consensus reject.
            if (rawTxBytes[i][0] < kRlpListBase && rawTxBytes[i][0] != 0x01 &&
                rawTxBytes[i][0] != 0x02 && rawTxBytes[i][0] != 0x04)
                throw OpConsensusError("op block: unsupported tx type byte 0x" +
                                       std::to_string(rawTxBytes[i][0]));
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                fee = op::loadOpFeeParams(stateView);
                if (cfg.has_da_footprint)
                {
                    auto const& attrData = deposits[0].data;
                    if (attrData.size() == op::IsthmusL1AttributesLen)
                        fee.da_footprint_gas_scalar = 0;
                    else if (attrData.size() >= op::JovianL1AttributesLen)
                        fee.da_footprint_gas_scalar = static_cast<uint16_t>(
                            (static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 2]) << 8) |
                            static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 1]));
                }
                feeLoaded = true;
            }
            if (i >= transactions.size())
                throw OpConsensusError("runOpBlockInjection: transactions list shorter than block");
            protocol::TransactionReceipt::Ptr receipt;
            try
            {
                receipt = bcos::task::syncWait(executor.executeTransaction(view, header,
                    *transactions[i], /*contextID=*/0, ledgerConfig, /*call=*/false, fee,
                    blockGasLeft, chainId, &hashes));
            }
            catch (const bcos::executor_v1::opstack::OpTxValidationFailed& e)
            {
                throw OpConsensusError(
                    "runOpBlockInjection: normal tx validation failed: " + std::string(e.what()));
            }
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(rawTxBytes[i][0] >= kRlpListBase ? 0 : rawTxBytes[i][0]);
        }
    }
    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));
    result.gasUsed = cumulative;
    if (hashErr.has_value())
        throw OpStorageError("runOpBlockInjection: block-hash lookup failed: " + *hashErr);

    // (4) commitments: MessagePasser snapshot -> seal -> stateRoot -> txRoot (unchanged).
    std::map<evmc::bytes32, evmc::bytes32> mpStorage;
    bcos::evm::evmstate::Storage2State<Storage> bridge(view);
    bridge.visitAccounts([&](auto const& acc) {
        if (acc.addr == op::OP_L2_TO_L1_MESSAGE_PASSER)
        {
            mpStorage = acc.storage;
            return false;
        }
        return true;
    });
    if (bridge.poisoned())
        throw OpStorageError("runOpBlockInjection: poisoned: " + std::string(bridge.firstError()));
    auto seal = op::sealOpBlock(result, cfg, mpStorage);
    auto root = bcos::evm::stateRootOf(bridge);
    if (bridge.poisoned())
        throw OpStorageError(
            "runOpBlockInjection: poisoned after stateRootOf: " + std::string(bridge.firstError()));
    auto txRoot = computeOpTxRoot(rawTxBytes);
    return OpExecuteBlockResult{std::move(result.receipts), seal, detail::toBcosH256(root),
        static_cast<uint64_t>(cumulative), txRoot};
}
```

`OpBlockTx` **保留**（route-A `processOpBlock`/`validateJovianBlockShape` 仍用）。删 `EnvelopeToTarsConverter` 别名（OpBlockExecute.h:161-162）——Task 3 后无生产/测试使用。

- [ ] **Step 2: 重写 `OpScheduler::execute` hook + 删 `m_envelopeToTars`**（OpScheduler.h）

```cpp
    // …execute hook 开头 rawTxBytes 提取不变…
    bcos::evm::engine::OpExecuteBlockResult result;
    try
    {
        const auto& cfg =
            op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

        // Classify by type byte: deposits decoded; normal txs consumed as the block's Transaction
        // objects (buildOpBlock already converted them via opEnvelopeToTars) — no raw parse.
        std::vector<op::DepositTx> deposits;
        deposits.reserve(rawTxBytes.size());
        for (auto const& raw : rawTxBytes)
        {
            if (raw.empty())  // 空 envelope：直接 raw[0] 会越界（旧 decodeOneRawTx 的 empty 拒绝保留）
                throw bcos::evm::engine::OpConsensusError("OpScheduler: empty envelope");
            auto const typeByte = raw[0];
            if (typeByte == static_cast<uint8_t>(op::kDepositTxType))
                deposits.push_back(detail::decodeDepositTx(raw));  // OpConsensusError on malformed
            else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 && typeByte != 0x04)
                throw bcos::evm::engine::OpConsensusError("OpScheduler: unsupported tx type byte 0x" +
                                                          std::to_string(typeByte));
        }

        bcos::ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setEVMCRevision(cfg.rev);

        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        result = bcos::evm::engine::runOpBlockInjection(executor, view, header, transactions,
            deposits, cfg, m_chainId, ledgerConfig, rawTxBytes, m_hashImpl);
    }
    catch (const bcos::evm::engine::OpConsensusError&) { throw; }   // 原有 catch 结构不变
    catch (const bcos::evm::engine::OpStorageError&) { throw; }
    catch (const std::exception&) { throw; }
    catch (...) { /* 原样保留 RTTI 兜底 */ }
```

删除：ctor 参数 `bcos::evm::engine::EnvelopeToTarsConverter envelopeToTars`（:568）与 `m_envelopeToTars(std::move(envelopeToTars))`（:575）；成员 `m_envelopeToTars`（:774）。**`#include <opstack-executor/OpTxDecode.h>`（:24）保留到 Task 4** —— 新 execute hook 调 `detail::decodeDepositTx`，后者到 Task 4 才搬进 OpBlockExecute.h；Task 3 期间 OpTxDecode.h 仍是唯一来源。

- [ ] **Step 3: `decodeDepositTx` 改返回 `DepositTx` + `decodeOneRawTx` deposit 分支适配**（OpTxDecode.h）

execute hook 的 `deposits.push_back(detail::decodeDepositTx(raw))` 需要 `decodeDepositTx` 直接返回 `DepositTx`。改 OpTxDecode.h：

```cpp
// 返回类型 OpBlockTx → DepositTx（原 body 不变，去 OpBlockTx 包装）
inline bcos::evm::opstack::DepositTx decodeDepositTx(bcos::bytes rawEntry)
{
    …
    return dep;  // 原为 OpBlockTx{.tx = std::move(dep), .signedEnvelope = {}}
}
```

`decodeOneRawTx` 的 deposit 分支（:397-398）同步改 —— 它仍返回 `OpBlockTx`（OpT8nReplayTest 到 Task 4 前还在用）：

```cpp
if (typeByte == kDepositTypeByte)
    return bcos::evm::opstack::OpBlockTx{
        .tx = decodeDepositTx(std::move(rawEntry)), .signedEnvelope = {}};
```

- [ ] **Step 4: 适配测试直接调用点**（本步骤结束时编译恢复绿）

**OpBlockInjectorTest.cpp**（`InjectsDepositAndEip1559Block` :189-198）——`txs/normalTxs` → `deposits/transactions(块序)`：

```cpp
    auto depTx = makeAttributesDeposit();   // OpBlockTx
    auto normTx = makeEip1559OpBlockTx();   // OpBlockTx
    std::vector<op::DepositTx> deposits{std::get<op::DepositTx>(depTx.tx)};
    // 块序 transactions：索引0 是 deposit 占位（deposit 分支不触碰），索引1 是 normal。
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions{
        nullptr, buildEip1559FiscoTx()};
    bcos::bytes depEnv(depTx.signedEnvelope.begin(), depTx.signedEnvelope.end());
    bcos::bytes normEnv(normTx.signedEnvelope.begin(), normTx.signedEnvelope.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, normEnv};
    auto result = engine::runOpBlockInjection(executor, storage, *header, transactions, deposits,
        cfg, kChainId, ledgerConfig, rawTxBytes, hashImpl);
```

`EmptyBlockRejectedByInjector`（:241-246）→ 空 `transactions`/`deposits`/`rawTxBytes` 三空传参。

**OpSchedulerTest.cpp** `runRouteBDirect`（:442-471）——删 `txs`（decodeOneRawTx）/`normalTxs`（makeConverter）循环，改：

```cpp
    std::vector<op::DepositTx> deposits;
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
    {
        if (raw[0] == static_cast<uint8_t>(op::kDepositTxType))
            deposits.push_back(detail::decodeDepositTx(raw));
        const auto txHash = f.hashImpl->hash(raw);
        auto tarsTx = bcos::engine::detail::opEnvelopeToTars(raw, txHash);
        if (!tarsTx)
        {  // 镜像 buildOpBlock fallback：minimal tx（hash + wire bytes）
            bcostars::Transaction fallback;
            fallback.extraTransactionHash.assign(txHash.begin(), txHash.end());
            fallback.extraTransactionBytes.assign(raw.begin(), raw.end());
            tarsTx = std::move(fallback);
        }
        tarsTx->extraTransactionBytes.assign(raw.begin(), raw.end());
        transactions.push_back(std::make_shared<bcostars::protocol::TransactionImpl>(
            [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; }));
    }
    …
    return bcos::evm::engine::runOpBlockInjection(executor, view, header, transactions, deposits,
        cfg, kChainId, ledgerConfig, rawTxBytes, f.hashImpl);
```

删 `makeConverter`（:139-146，include `EngineServiceImpl.h` 保留供 `opEnvelopeToTars`）。`:486`/`:618` 的 `canonicalEnvelopeBytes(OpBlockTx{...})` 本任务**保留**（OpTxDecode.h 仍在；Task 4 换成测试 helper）。

**OpDualPathEquivalenceTest.cpp** route B（:1019-1065）——删 decodeOneRawTx txs 循环 + buildFiscoTx normalTxs 循环，改：

```cpp
    // deposits + block-order transactions（buildBlockTx 已支持 deposit，Web3Transaction 0x7e）。
    std::vector<op::DepositTx> deposits;
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
    {
        if (raw[0] == static_cast<uint8_t>(op::kDepositTxType))
            deposits.push_back(detail::decodeDepositTx(raw));
        auto tx = buildBlockTx(raw, fixture.hashImpl);
        if (tx == nullptr) { BOOST_ERROR(id << ": buildBlockTx failed"); return; }
        transactions.push_back(tx);
    }
    …
    resultB = engine::runOpBlockInjection(executor, viewB, *header, transactions, deposits, cfg,
        kChainId, ledgerConfig, rawTxBytes, fixture.hashImpl);
```

route-A（:1092-1105）块装配**不变**（已用 buildBlockTx 全量；scheduler execute hook 内部自己 decode deposits）。**删除 `buildFiscoTx`**（:572-623，含 pre-flight `decodeOneRawTx` 比较），并同步删随之孤立的 `evmoneTxFieldDiff`（:474）/`evmoneTxFieldsEqual`（:530）两个 static helper（否则 -Wunused-function/-Werror）。

**OpScheduler ctor 参数删除波及的 3 处未列调用点**（`m_envelopeToTars` 参数移除后必须同步删实参）：
- `OpNewPayloadRpcE2eTest.cpp:213-218`（fixture 构造 OpScheduler 时传的 converter lambda :215-217）。
- `OpDualPathEquivalenceTest.cpp:1107-1113`（route-A 的 `OpScheduler` 构造传 converter lambda :1110-1112）——Task 3 Files 需加这两个文件。
- `OpSchedulerTest.cpp:326-327`（`TestOpScheduler(..., makeConverter(), ...)` fixture）——与"删 makeConverter"联动。

**`OpDualPathEquivalenceTest.cpp:1175` `hasStorageScan(id, txs)`**：route-B 重写删掉 `txs`（vector\<OpBlockTx\>）后此引用变未定义。改为传 `deposits` + `transactions`（或按该 helper 实际扫描的 deposit 账户直接传 deposits 构造的地址集），Task 3 一并适配。

- [ ] **Step 5: 编译 + 跑 opstack 全量测试**

Run: 构建 opstack-executor lib + libinitializer；`rtk cargo test -p bcos-opstack-executor`
Expected: 编译绿；`OpSchedulerTest`/`OpBlockInjectorTest`/`OpDualPathEquivalenceTest` 全 PASS（DIVERGE=0）。`OpT8nReplayTest` 不受影响（`decodeOneRawTx` 仍在，Task 4 才删）。

- [ ] **Step 6: 提交**

```bash
git add -u
git commit -m "refactor(opstack): route B 消费块 Transaction 对象，runOpBlockInjection 改 transactions+deposits 形状"
```

---

### Task 4: `OpTxDecode.h` 收尾 —— decodeDepositTx 搬迁 + 删 decodeOneRawTx/decoders + 测试 helper

**Files:**
- Modify: `opstack-executor/OpBlockExecute.h`（`bcos::evm::engine::detail` 追加 `decodeDepositTx`；若需 `#include <bcos-evm/eth/RlpEncodeTuple.h>` 供 helper 则一并）
- Delete: `opstack-executor/OpTxDecode.h`
- Create: `opstack-executor/tests/support/OpDepositEncode.h`（`encodeDepositEnvelope(DepositTx) → bcos::bytes`，0x7e || rlp([8 fields])，从 `canonicalEnvelopeBytes` deposit 分支搬出）
- Modify: `opstack-executor/OpScheduler.h`（已删 include；确认无残留）
- Modify: `opstack-executor/OpSchedulerImpl.h`（删 `#include <opstack-executor/OpRlpDecode.h>` 残留 + 过时 detail 注释）
- Modify: `opstack-executor/tests/OpSchedulerTest.cpp`（`:486`/`:618` `canonicalEnvelopeBytes` → `encodeDepositEnvelope`）
- Modify: `opstack-executor/tests/OpT8nReplayTest.cpp`（decode-class reject（blob）repro 从 `decodeOneRawTx` 改为类型字节拒绝路径）

**Interfaces:**
- Consumes: Task 3 的 `decodeDepositTx`（已在 OpTxDecode.h，本任务搬到 OpBlockExecute.h，返回 `DepositTx`）。
- Produces: `bcos::evm::engine::detail::decodeDepositTx(bcos::bytes rawEntry) → DepositTx`（OpBlockExecute.h）；`tests/support/OpDepositEncode.h::encodeDepositEnvelope`。`OpTxDecode.h` 删除后，`OpScheduler.h` 不再 include 它（Task 3 已删）。

- [ ] **Step 1: 搬 `decodeDepositTx` 到 OpBlockExecute.h**（返回类型已是 `DepositTx`，Task 3 改过）

从 OpTxDecode.h 剪切 `decodeDepositTx` 函数体到 OpBlockExecute.h 的 `bcos::evm::engine::detail`（RLP 基元经已 include 的 OpRlpDecode.h 复用；`OpBlockTx` 定义同文件可见）。函数体不变（只把返回从 `OpBlockTx` 换成 `DepositTx`，去掉 `.tx = std::move(dep), .signedEnvelope = {}` 包装，直接 `return dep;`）。

- [ ] **Step 2: 建测试 helper `tests/support/OpDepositEncode.h`**

```cpp
// 0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTransaction, data]) —
// canonicalEnvelopeBytes 的 deposit 分支（OpTxDecode.h:307 删除后测试专用）。
inline bcos::bytes encodeDepositEnvelope(const bcos::evm::opstack::DepositTx& d)
{
    using bcos::evm::eth::detail::encodeTuple;
    auto body = encodeTuple(evmc::bytes_view(d.source_hash), evmc::bytes_view(d.from),
        d.to.has_value() ? evmc::bytes_view(*d.to) : evmc::bytes_view{}, d.mint.value_or(0),
        d.value, static_cast<uint64_t>(d.gas_limit),
        static_cast<uint64_t>(d.is_system_tx ? 1 : 0), d.data);
    bcos::bytes out;
    out.reserve(body.size() + 1);
    out.push_back(0x7e);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
```

- [ ] **Step 3: 删 `OpTxDecode.h` 其余内容 + 删文件**

删 `decodeOneRawTx`/`decodeEip1559Tx`/`decodeSetCodeTx`/`decodeAccessListTx`/`decodeLegacyTx`/`canonicalEnvelopeBytes`/`assertCanonicalRoundTrip` 及文件。同步处理所有 include 与消费点：
- `OpScheduler.h` **删 `#include <opstack-executor/OpTxDecode.h>`**（从 Task 3 移来，此时 `decodeDepositTx` 已在 OpBlockExecute.h）。
- 测试 include：`OpSchedulerTest.cpp:26`、`OpDualPathEquivalenceTest.cpp:74`、`OpT8nReplayTest.cpp:35` 的 `#include <opstack-executor/OpTxDecode.h>` 删除（后两者改 include `tests/support/OpDepositEncode.h` 若需 `encodeDepositEnvelope`）。
- `canonicalEnvelopeBytes` 消费点：`OpSchedulerTest` `:486`/`:618` **与 `OpDualPathEquivalenceTest.cpp:457`**（`buildRawTxBytes` 的 deposit 分支，被 `runChainVector` :1269 使用，非被删枚举）全部改 `encodeDepositEnvelope`。
- `OpSchedulerImpl.h` 删 `#include <opstack-executor/OpRlpDecode.h>` 与过时 detail 注释。

**OpDualPathEquivalenceTest.cpp 的 "Task 3 Step 4" corpus 枚举（:1362-1459）**：该枚举用 `decodeOneRawTx`（:1426）对比 `opEnvelopeToTars` 的接受集并断言"0 个被 strict 接受但被 lenient 拒"——重构后只剩单一路径（`opEnvelopeToTars` + 类型字节 + `validateEnvelopeSignature`），该对比失去第一操作数，**删除整个枚举用例与其断言**（:1459）；其职责由 corpus 的拒绝断言 + Task 1/2 的回归钉覆盖。

- [ ] **Step 4: 适配 `OpT8nReplayTest` 的 decode-class reject（blob）**

blob（0x03）的拒绝从"`decodeOneRawTx` 抛 `unsupported tx type byte 0x03`"改为"类型字节检查抛 `OpConsensusError`"。`loadBlockContext` 的 `decodeRejectMessage`（:529-533）与记录点（:746-760）改为：0x03 信封经新 `runOpBlockInjection` 类型字节分支抛 `OpConsensusError("op block: unsupported tx type byte 0x3")`（或直接断言 execute hook 分类逻辑）。若该测试的 blob 向量断言依赖具体消息文本，同步更新。

- [ ] **Step 5: 编译 + 全量测试**

Run: 构建 opstack-executor + libinitializer；`rtk cargo test -p bcos-opstack-executor`
Expected: 全绿；`OpT8nReplayTest` 恢复通过。

- [ ] **Step 6: 提交**

```bash
git add -u
git commit -m "refactor(opstack): 删 OpTxDecode.h，decodeDepositTx 并入 OpBlockExecute.h"
```

---

### Task 5: 全量验证 + 收尾提交

**Files:** 无新改动（仅验证）。

- [ ] **Step 1: 全量构建**

Run: 构建 `opstack-executor`、`libinitializer`、`engine` 三个目标 + 三个测试目标（`OpSchedulerTest`/`OpBlockInjectorTest`/`OpDualPathEquivalenceTest`/`OpT8nReplayTest`/`OpRlpDecodeTest`/`OpstackExecutorTest`）。
Expected: 编译零告警。

- [ ] **Step 2: 跑全部相关测试 + 语料 gate**

Run: 上述测试目标 + `OpEnvelopeToTarsTest`（未改）+ t8n/语料相关 target。
Expected: 全绿；双路径 DIVERGE=0；131 向量语料 gate 通过。

- [ ] **Step 3: 确认共识检查无漏网**

Run: `validateEnvelopeSignature` 单元测试 + Task 2 回归用例全 PASS；grep 确认生产代码无 `decodeOneRawTx` 残留。

- [ ] **Step 4: 收尾提交（若 Step 1-3 有格式/遗漏改动）**

```bash
git add -u
git commit -m "chore(opstack): 验证 OP 块执行消费 Transaction 对象重构"
```

---

## 自审记录

- **Spec 覆盖**：§3.1 数据流→Task 3；§3.2 三项检查→Task 1+2；§3.3 错误分类→Task 3（类型字节出口 + opValidate 出口 + 空 envelope 守卫）；§3.4 文件收尾→Task 4；§4 测试→Task 1/2/3/4（buildFiscoTx pre-flight 删除→Task 3；OpT8nReplayTest blob repro→Task 4；dual-path corpus 枚举删除→Task 4）。§2.2 范围外项均未触碰（`buildOpBlock`/`opEnvelopeToTars`/`processOpBlock`/`OpBlockTx`）。
- **占位扫描**：无 TBD/TODO；每步含具体代码或精确引用。
- **类型一致性**：`runOpBlockInjection` 新签名（transactions/deposits/rawTxBytes）在 Task 3 全调用点一致；`decodeDepositTx → DepositTx` 在 Task 3（改返回）+ Task 4（搬迁）一致；`validateEnvelopeSignature` 在 Task 1 定义、Task 2 消费（`envRef.toBytes()` 桥接 `bytesConstRef`）。

## 4-agent 审查修订记录（2026-08-13）

启动 4 个 sub-agent（代码锚点 / 共识安全 / 计划完整性 / spec↔plan 一致性）交叉核对 spec+plan 后合并修订：
- **design**：canonical 兜底归因修正（六路比较不参与 → validateEnvelopeSignature 层1+2 + step-2 blockHash）；`opEnvelopeToTars` 已拒 yParity 的前提修正；route-A OpBlockTx 重建说法删除；语料 125→131；`m_prepare` 双参数；legacy v==27/28；空 envelope 出口；`opEnvelopeToTars` throw 路径 pre-existing 注明。
- **plan**：Task 2 `bytesConstRef→toBytes` + 现有 OpstackExecutorTest 用例修复；Task 3 空 envelope 守卫 + 3 处未列 ctor 调用点（OpNewPayloadRpcE2eTest:213-218 / OpDualPathEquivalenceTest:1107-1113 / OpSchedulerTest:326-327）+ `hasStorageScan` :1175 + unused-function :474/:530 + include 时序修正；Task 4 三测试 include + dual-path :457 `canonicalEnvelopeBytes` 迁移。
- 上述修复全部落在既有 Task 3/4 内，无需新 Task。
