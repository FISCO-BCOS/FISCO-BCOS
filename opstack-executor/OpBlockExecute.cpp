#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-rlp-protocol/Web3TxEnvelope.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpstackExecutor.h>  // envelopeExecutionFieldsMismatch (shared gate)
#include <algorithm>
#include <bcos-evm/eth/state/state.hpp>  // evmone::state::finalize
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <charconv>
#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>
#include <system_error>

// isL1AttributesTx lives in OpBlockExecute.h; narrowGasUsed / decimalCumulative live in OpCommon.h
// (both shared with the per-tx loop — one implementation, no copy drift).

namespace bcos::evm::opstack
{
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg)
{
    if (!cfg.has_da_footprint)  // Jovian-only (op-geth CalcDAFootprint)
        return;
    if (txs.empty())  // rejected by processOpBlock anyway
        return;
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        return;
    validateJovianL1AttributesShape(
        std::span<uint8_t const>{firstDep->data.data(), firstDep->data.size()},
        std::holds_alternative<DepositTx>(txs.back().tx), cfg);
}

evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        // runtime_error (not logic_error): block-level rejection (INVALID), not a local fault.
        throw OpConsensusError("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // Storage write-back failures must leave as OpStorageError (-32603), never a bare
    // runtime_error — the same classification the per-tx path applies in m_finish /
    // executeDeposit / finalizeBlock (Storage2State::applyDiff poisons AND rethrows raw).
    // Without this normalization, a storage fault on the block path would escape unclassified
    // and diverge from the documented error contract.
    auto applyDiffChecked = [&](const evmone::state::StateDiff& diff) {
        try
        {
            applyDiff(diff);
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            throw bcos::evm::engine::OpStorageError(
                std::string("op block: storage write-back failed: ") + e.what());
        }
        catch (...)
        {
            throw bcos::evm::engine::OpStorageError(
                "op block: storage write-back failed: unknown exception");
        }
    };

    // Step 1: pre-block system call (4788/2935; gating/skip handled inside evmone).
    applyDiffChecked(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // Step 2: first tx must be a deposit (hard reject) + L1-attributes content (warn, op-geth
    // accept-at-validation) + Jovian shape. Accept set mirrors preBlockOpSteps / ExecuteContext;
    // seenNonDeposit is set pre-validation here while ExecuteContext sets it after a successful
    // prepare — any validation failure aborts the whole block on this path, so the timing
    // difference has no effect.
    if (txs.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        throw OpConsensusError("op block: first tx is not a deposit");
    if (!isL1AttributesTx(*firstDep))
        BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                          << "op block: first tx is a deposit but not the L1 attributes tx — "
                             "accepted";
    validateJovianBlockShape(txs, cfg);

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    result.txTypes.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    size_t transactionIndex = 0;
    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                BCOS_LOG(WARNING) << LOG_BADGE("OP_BLOCK_EXEC")
                                  << "deposit after non-deposit in block — accepted";
            evmone::state::StateDiff diff;
            auto receipt = [&]() {
                try
                {
                    return runDeposit(view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft,
                        receiptFactory, diff);
                }
                catch (const OpConsensusError&)
                {
                    throw;
                }
                catch (const std::runtime_error& e)
                {
                    throw OpConsensusError(
                        std::string("op block: deposit execution failed: ") + e.what());
                }
            }();
            applyDiffChecked(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            // Store cumulative gas as decimal; RPC parses that field as decimal.
            receipt->setCumulativeGasUsed(decimalCumulative(static_cast<uint64_t>(cumulative)));
            receipt->setTransactionIndex(transactionIndex++);
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(classifyTxType(static_cast<uint8_t>(kDepositTxType)));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // Fee params lazily loaded at the first normal tx (op-geth's per-block cache).
                // loadOpFeeParams reads the L1Block predeploy storage slots — same source as
                // op-geth's rollup cost, which also reads the L1Block predeploy (written from
                // the L1 attributes deposit) rather than re-parsing the deposit calldata. The
                // Jovian-only DA scalar is the one deliberate exception: it is read directly
                // from calldata[176:178] so it stays authoritative even if the attributes
                // deposit rolled back L1Block slot8; the activation block (176B) forces 0.
                fee = loadOpFeeParams(view);
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (auto scalar = jovianDaFootprintGasScalar(
                            std::span<uint8_t const>{attrData.data(), attrData.size()}))
                        fee.da_footprint_gas_scalar = *scalar;
                }
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto const envRef =
                bcos::bytesConstRef(btx.signedEnvelope.data(), btx.signedEnvelope.size());
            if (auto mismatch =
                    bcos::executor_v1::opstack::envelopeChainIdMismatch(envRef, chainId))
            {
                throw OpConsensusError("op block: " + *mismatch);
            }
            // Fail-closed mirror↔envelope cross-check — the SAME gate the per-tx path runs in
            // m_prepare (OpstackExecutor.h): execution fields (nonce/gasLimit/to/value/data)
            // must match the signed envelope, never the forgeable mirror. txTypes committed to
            // the receipts root below depend on tx.type, so an unbound mirror here would poison
            // the block's header commitment exactly like the per-tx path.
            if (auto mismatch =
                    bcos::executor_v1::opstack::envelopeExecutionFieldsMismatch(envRef, tx))
            {
                throw OpConsensusError(
                    "op block: tx execution fields diverge from the signed envelope: " + *mismatch);
            }
            if (auto missing = bcos::executor_v1::opstack::blockPathZeroSender(tx.sender))
            {
                throw OpConsensusError("op block: " + *missing);
            }
            if (auto unbound = bcos::executor_v1::opstack::blockPathUnboundAuthorizationList(tx))
            {
                throw OpConsensusError("op block: " + *unbound);
            }
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // No failed-receipt mechanism for normal txs: void the whole block (op-geth).
                throw OpConsensusError("op block: invalid non-deposit tx: " + err->message());
            // opTransition charges from props.fee (the validate-time snapshot — no second read).
            evmone::state::StateDiff diff;
            auto receipt = [&]() {
                try
                {
                    return opTransition(view, block, hashes, tx, cfg, vm,
                        std::get<OpTxProperties>(v), chainId, receiptFactory, diff);
                }
                catch (const OpConsensusError&)
                {
                    throw;
                }
                catch (const std::runtime_error& e)
                {
                    throw OpConsensusError(
                        std::string("op block: transaction execution failed: ") + e.what());
                }
            }();
            applyDiffChecked(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(decimalCumulative(static_cast<uint64_t>(cumulative)));
            receipt->setTransactionIndex(transactionIndex++);
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(classifyTxType(static_cast<uint8_t>(tx.type)));
        }
    }

    // Step 4: end-of-block finalize.
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiffChecked(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}

// ---- block-header seal ----
namespace
{
/// Parse cumulativeGasUsed: 0x-hex via safeFromQuantity, otherwise decimal.
/// Bare digits must not go to safeFromQuantity (it treats them as hex).
[[nodiscard]] uint64_t parseCumulativeGasUsed(std::string_view s)
{
    if (s.size() > 1 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
    {
        if (auto v = bcos::safeFromQuantity(s))
            return *v;
    }
    else
    {
        uint64_t value = 0;
        const auto* begin = s.data();
        const auto* end = begin + s.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value, 10);
        if (ec == std::errc{} && ptr == end)
            return value;
    }
    throw OpConsensusError(
        "op block: invalid cumulativeGasUsed in receipt (not hex or decimal): " + std::string(s));
}

/// Patch a placeholder list-header byte at headerPos with the canonical header for payloadLen
/// (short form for < 56 bytes, long form otherwise — the long form inserts the length bytes,
/// shifting the already-written payload once per list, not once per field).
inline void patchRlpListHeader(bcos::bytes& buf, size_t headerPos, size_t payloadLen)
{
    if (payloadLen < 56)
    {
        buf[headerPos] = static_cast<bcos::byte>(0xc0 + payloadLen);
        return;
    }
    bcos::bytes lenBytes;
    auto v = payloadLen;
    while (v > 0)
    {
        lenBytes.insert(lenBytes.begin(), static_cast<bcos::byte>(v & 0xff));
        v >>= 8;
    }
    buf[headerPos] = static_cast<bcos::byte>(0xf7 + lenBytes.size());
    buf.insert(
        buf.begin() + static_cast<ptrdiff_t>(headerPos) + 1, lenBytes.begin(), lenBytes.end());
}

/// RLP list of logs: [address, [topics...], data] each, whole collection wrapped in a list
/// (byte-identical to evmone's rlp::encode_container over vector<Log>). Writes each log's
/// bytes once, with header backfill — no per-log intermediate buffers.
inline void encodeLogsList(bcos::bytes& to, gsl::span<const bcos::protocol::LogEntry> logs)
{
    auto const listStart = to.size();
    to.push_back(0xc0);  // placeholder (patched below)
    auto const payloadStart = to.size();
    for (const auto& log : logs)
    {
        auto const logStart = to.size();
        to.push_back(0xc0);  // placeholder for this log's list header
        bcos::codec::rlp::encode(to, log.address());
        auto const topicsStart = to.size();
        to.push_back(0xc0);  // placeholder for the topics list header
        for (const auto& topic : log.topics())
            bcos::codec::rlp::encode(to, topic);
        patchRlpListHeader(to, topicsStart, to.size() - topicsStart - 1);
        bcos::codec::rlp::encode(to, log.data());
        patchRlpListHeader(to, logStart, to.size() - logStart - 1);
    }
    patchRlpListHeader(to, listStart, to.size() - payloadStart);
}
}  // namespace

evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Secure trie over the live slot map (key = keccak256(slot), leaf = rlp(trimmed value)).
    std::map<bcos::h256, bcos::bytes> entries;
    for (const auto& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        size_t first = 0;
        while (first < sizeof(value.bytes) && value.bytes[first] == 0)
        {
            ++first;
        }
        bcos::bytes leaf;
        bcos::codec::rlp::encode(
            leaf, bcos::bytes(value.bytes + first, value.bytes + sizeof(value.bytes)));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}

bcos::bytes encodeReceiptForRoot(const bcos::protocol::TransactionReceipt& r, uint8_t txType)
{
    // RLP bool semantics: true → 0x01, false → 0x80 (raw push; the UnsignedByte encode path
    // mis-handles bool). Payload = rlp([status, cumGas, bloom, logs]) + (deposit) [nonce, version].
    const bool success = (r.status() == 0);
    const uint64_t cumGas = parseCumulativeGasUsed(r.cumulativeGasUsed());
    const auto bloom = r.logsBloom();
    if (bloom.size() != 256)
    {
        throw OpConsensusError(
            "op block: receipt logsBloom must be 256 bytes, got " + std::to_string(bloom.size()));
    }
    const auto logs = r.logEntries();

    bcos::bytes payload;
    payload.push_back(success ? 0x01 : 0x80);
    bcos::codec::rlp::encode(payload, cumGas);
    bcos::codec::rlp::encode(payload, bloom);
    encodeLogsList(payload, logs);

    if (txType == static_cast<uint8_t>(kDepositTxType))
    {
        const auto& meta = r.opStackMeta();
        if (!meta || !meta->deposit_nonce || !meta->deposit_receipt_version)
            throw OpConsensusError(
                "op block: deposit receipt missing deposit nonce/receipt version");
        bcos::codec::rlp::encode(payload, *meta->deposit_nonce);
        bcos::codec::rlp::encode(payload, *meta->deposit_receipt_version);
    }

    bcos::bytes out;
    out.reserve(payload.size() + 4);
    // Typed raw-byte prefix (legacy has none; deposit's 0x7e is the EIP-2718 type byte).
    if (txType != static_cast<uint8_t>(evmone::state::Transaction::Type::legacy))
        out.push_back(txType);
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    // Guard before any indexing: a caller that builds an OpBlockResult by hand (part-5's
    // OpScheduler, or a test) could otherwise feed mismatched receipts/txTypes — txTypes[i] is
    // indexed below and its bytes become the receipts-root leaf's type prefix, so a length
    // mismatch would silently produce a wrong header commitment (an out-of-bounds read). This is
    // a caller programming error (internal invariant), not a block-content rejection — mapped to
    // std::logic_error, never INVALID.
    if (result.txTypes.size() != result.receipts.size())
        throw std::logic_error("op block: receipts/txTypes length mismatch (caller bug)");
    OpBlockSeal seal{};

    // receipts-root: var-key trie (key = rlp(index), leaf = EncodeIndex encoding).
    std::vector<std::pair<bcos::bytes, bcos::bytes>> receiptsEntries;
    receiptsEntries.reserve(result.receipts.size());
    for (size_t i = 0; i < result.receipts.size(); ++i)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, static_cast<uint64_t>(i));
        auto leaf = encodeReceiptForRoot(*result.receipts[i], result.txTypes[i]);
        receiptsEntries.emplace_back(std::move(key), std::move(leaf));
    }
    auto receiptsResult = bcos::ledger::mpt::computeTrieRootVarKey(receiptsEntries);
    std::memcpy(
        seal.receiptsRoot.bytes, receiptsResult.root.data(), sizeof(seal.receiptsRoot.bytes));

    // Block-level logsBloom = bitwise-OR of each receipt's 256-byte bloom.
    for (const auto& r : result.receipts)
    {
        const auto bloom = r->logsBloom();
        if (bloom.size() != 256)
        {
            throw OpConsensusError("op block: receipt logsBloom must be 256 bytes, got " +
                                   std::to_string(bloom.size()));
        }
        for (size_t i = 0; i < 256; ++i)
            seal.logsBloom.bytes[i] |= bloom[i];
    }

    // Isthmus+: withdrawalsRoot = MessagePasser storage root, requestsHash = sha256("").
    // Pre-Isthmus: withdrawals list is always empty → empty-trie root; no requests field.
    if (cfg.fork >= OpFork::Isthmus)
    {
        seal.withdrawalsRoot = opStorageRoot(messagePasserStorage);
        seal.requestsHash = OP_EMPTY_REQUESTS_HASH;
    }
    else
    {
        auto const emptyRoot = bcos::ledger::mpt::emptyRootHash();
        std::memcpy(
            seal.withdrawalsRoot.bytes, emptyRoot.data(), sizeof(seal.withdrawalsRoot.bytes));
    }

    // Jovian: header blobGasUsed slot = DA footprint (Σ da_footprint over non-deposit receipts).
    // Deposits legitimately carry nullopt and are skipped. A missing optional on a non-deposit
    // receipt must not silently contribute 0 — that under-counts the header commitment the
    // same way a missing deposit nonce used to under-encode the receipts-root leaf.
    if (cfg.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (size_t i = 0; i < result.receipts.size(); ++i)
        {
            if (result.txTypes[i] == static_cast<uint8_t>(kDepositTxType))
                continue;
            const auto& meta = result.receipts[i]->opStackMeta();
            if (!meta || !meta->da_footprint)
                throw OpConsensusError(
                    "op block: non-deposit receipt missing da_footprint under Jovian");
            const auto term = *meta->da_footprint;
            if (footprint > std::numeric_limits<uint64_t>::max() - term)
                throw OpConsensusError("op block: DA footprint overflows uint64");
            footprint += term;
        }
        seal.blobGasUsed = footprint;
    }
    return seal;
}
}  // namespace bcos::evm::opstack
