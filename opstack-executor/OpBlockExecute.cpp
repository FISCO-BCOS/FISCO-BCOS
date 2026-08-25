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
#include <algorithm>
#include <bcos-evm/eth/state/state.hpp>  // evmone::state::finalize
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

// isL1AttributesTx lives in OpBlockExecute.h; narrowGasUsed / hexCumulative live in OpCommon.h
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

    const auto& data = firstDep->data;
    if (data.size() == IsthmusL1AttributesLen)
    {
        // Jovian activation block: Isthmus-length attributes, must be deposits-only (op-geth
        // rollup_cost.go:568-576). Checking the last tx suffices (deposits always precede
        // non-deposits).
        if (!std::holds_alternative<DepositTx>(txs.back().tx))
            throw std::runtime_error(
                "op block: unexpected non-deposit transactions in Jovian activation block");
        return;
    }

    // Normal Jovian block: L1 attributes must carry the Jovian selector and be ≥ Jovian length.
    if (data.size() < JovianL1AttributesLen)
        throw std::runtime_error(
            "op block: L1 attributes transaction data too short for DA footprint gas scalar");
    if (!std::equal(
            JovianL1AttributesSelector.begin(), JovianL1AttributesSelector.end(), data.begin()))
        throw std::runtime_error(
            "op block: L1 attributes transaction data does not have Jovian selector");
}

evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase)
{
    if (!cfg.disable_prague_requests)
        // runtime_error (not logic_error): block-level rejection (INVALID), not a local fault.
        throw std::runtime_error("op finalize: prague requests unsupported on OP chains");
    return bcos::evm::sanitizeStateDiff(
        view, evmone::state::finalize(view, cfg.rev, coinbase, std::nullopt, {}, {}));
}

OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff)
{
    // Step 1: pre-block system call (4788/2935; gating/skip handled inside evmone).
    applyDiff(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // Step 2: first tx must be a deposit (hard reject) + L1-attributes content (warn, op-geth
    // accept-at-validation) + Jovian shape. Same accept set as preBlockOpSteps / ExecuteContext.
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        throw std::runtime_error("op block: first tx is not a deposit");
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
            auto receipt = runDeposit(
                view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft, receiptFactory, diff);
            applyDiff(diff);
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
                // Fee params lazily loaded at the first normal tx (op-geth's per-block cache). DA
                // scalar reads calldata[176:178] (authoritative even if the attributes deposit
                // rolled back L1Block slot8); activation block (176B) forces 0.
                fee = loadOpFeeParams(view);
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (attrData.size() == IsthmusL1AttributesLen)
                        fee.da_footprint_gas_scalar = 0;
                    else if (attrData.size() >= JovianL1AttributesLen)
                    {
                        fee.da_footprint_gas_scalar = static_cast<uint16_t>(
                            (static_cast<uint16_t>(attrData[JovianL1AttributesLen - 2]) << 8) |
                            static_cast<uint16_t>(attrData[JovianL1AttributesLen - 1]));
                    }
                }
                feeLoaded = true;
            }
            const auto& tx = std::get<evmone::state::Transaction>(btx.tx);
            const evmc::bytes_view env{btx.signedEnvelope.data(), btx.signedEnvelope.size()};
            auto const envRef =
                bcos::bytesConstRef(btx.signedEnvelope.data(), btx.signedEnvelope.size());
            auto const envelopeChainId = bcos::rlp::protocol::web3ChainIdFromEnvelope(envRef);
            if (envelopeChainId.has_value() && *envelopeChainId != chainId)
            {
                throw std::runtime_error("op block: tx envelope chain_id " +
                                         std::to_string(*envelopeChainId) +
                                         " does not match node chainId " + std::to_string(chainId));
            }
            if (!envelopeChainId.has_value() && bcos::rlp::protocol::isTypedWeb3Envelope(envRef))
            {
                throw std::runtime_error(
                    "op block: typed tx envelope is missing a parseable chainId");
            }
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // No failed-receipt mechanism for normal txs: void the whole block (op-geth).
                throw std::runtime_error("op block: invalid non-deposit tx: " + err->message());
            // opTransition charges from props.fee (the validate-time snapshot — no second read).
            evmone::state::StateDiff diff;
            auto receipt = opTransition(view, block, hashes, tx, cfg, vm,
                std::get<OpTxProperties>(v), chainId, receiptFactory, diff);
            applyDiff(diff);
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
    applyDiff(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}

// ---- block-header seal ----
namespace
{
/// Parse cumulativeGasUsed: 0x-hex via safeFromQuantity, otherwise decimal.
/// Bare digits must not go to safeFromQuantity (it treats them as hex).
[[nodiscard]] uint64_t parseHexUint64(std::string_view s)
{
    if (s.size() > 1 && (s[0] == '0') && (s[1] == 'x' || s[1] == 'X'))
    {
        if (auto v = bcos::safeFromQuantity(s))
            return *v;
    }
    else
    {
        try
        {
            return boost::lexical_cast<uint64_t>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {}
    }
    throw std::runtime_error(
        "op block: invalid cumulativeGasUsed in receipt (not hex or decimal): " + std::string(s));
}

/// RLP list of logs: [address, [topics...], data] each, whole collection wrapped in a list
/// (byte-identical to evmone's rlp::encode_container over vector<Log>).
inline void encodeLogsList(bcos::bytes& to, gsl::span<const bcos::protocol::LogEntry> logs)
{
    bcos::bytes content;
    for (const auto& log : logs)
    {
        bcos::bytes entry;
        bcos::codec::rlp::encode(entry, log.address());
        bcos::bytes topics;
        for (const auto& topic : log.topics())
            bcos::codec::rlp::encode(topics, topic);
        bcos::codec::rlp::encodeHeader(entry, {.isList = true, .payloadLength = topics.size()});
        entry.insert(entry.end(), topics.begin(), topics.end());
        bcos::codec::rlp::encode(entry, log.data());
        bcos::codec::rlp::encodeHeader(content, {.isList = true, .payloadLength = entry.size()});
        content.insert(content.end(), entry.begin(), entry.end());
    }
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = content.size()});
    to.insert(to.end(), content.begin(), content.end());
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

evmc::bytes encodeReceiptForRoot(const bcos::protocol::TransactionReceipt& r, uint8_t txType)
{
    // RLP bool semantics: true → 0x01, false → 0x80 (raw push; the UnsignedByte encode path
    // mis-handles bool). Payload = rlp([status, cumGas, bloom, logs]) + (deposit) [nonce, version].
    const bool success = (r.status() == 0);
    const uint64_t cumGas = parseHexUint64(r.cumulativeGasUsed());
    const auto bloom = r.logsBloom();
    const auto logs = r.logEntries();

    bcos::bytes payload;
    payload.push_back(success ? 0x01 : 0x80);
    bcos::codec::rlp::encode(payload, cumGas);
    bcos::codec::rlp::encode(payload, bloom);
    encodeLogsList(payload, logs);

    if (txType == static_cast<uint8_t>(kDepositTxType))
    {
        const auto& meta = r.opStackMeta();
        const uint64_t nonce = (meta && meta->deposit_nonce) ? *meta->deposit_nonce : uint64_t{0};
        const uint64_t version =
            (meta && meta->deposit_receipt_version) ? *meta->deposit_receipt_version : uint64_t{0};
        bcos::codec::rlp::encode(payload, nonce);
        bcos::codec::rlp::encode(payload, version);
    }

    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    // typed raw-byte prefix (legacy has none; deposit's 0x7e is the EIP-2718 type byte).
    if (txType != static_cast<uint8_t>(evmone::state::Transaction::Type::legacy))
        out.insert(out.begin(), txType);
    return evmc::bytes(out.begin(), out.end());
}

OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    OpBlockSeal seal{};

    // receipts-root: var-key trie (key = rlp(index), leaf = EncodeIndex encoding).
    std::vector<std::pair<bcos::bytes, bcos::bytes>> receiptsEntries;
    receiptsEntries.reserve(result.receipts.size());
    for (size_t i = 0; i < result.receipts.size(); ++i)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, static_cast<uint64_t>(i));
        auto const leaf = encodeReceiptForRoot(*result.receipts[i], result.txTypes[i]);
        receiptsEntries.emplace_back(std::move(key), bcos::bytes{leaf.begin(), leaf.end()});
    }
    auto receiptsResult = bcos::ledger::mpt::computeTrieRootVarKey(receiptsEntries);
    std::memcpy(
        seal.receiptsRoot.bytes, receiptsResult.root.data(), sizeof(seal.receiptsRoot.bytes));

    // Block-level logsBloom = bitwise-OR of each receipt's 256-byte bloom.
    for (const auto& r : result.receipts)
    {
        const auto bloom = r->logsBloom();
        for (size_t i = 0; i < 256 && i < bloom.size(); ++i)
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

    // Jovian: header blobGasUsed slot = DA footprint (Σ da_footprint over non-deposit receipts;
    // deposits carry nullopt, so summing every receipt is equivalent).
    if (cfg.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (const auto& r : result.receipts)
        {
            const auto& meta = r->opStackMeta();
            if (meta && meta->da_footprint)
                footprint += *meta->da_footprint;
        }
        seal.blobGasUsed = footprint;
    }
    return seal;
}
}  // namespace bcos::evm::opstack
