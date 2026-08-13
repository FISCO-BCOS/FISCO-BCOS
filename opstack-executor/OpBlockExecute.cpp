#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-ledger/mpt/HashBuilder.h>
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

// isL1AttributesTx / narrowGasUsed / hexCumulative were exported to OpBlockExecute.h
// (shared with runOpBlockInjection — one implementation, no copy drift);
// the anonymous-namespace copies are removed so processOpBlock sees exactly one definition.

namespace bcos::evm::opstack
{
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg)
{
    // Jovian-only: op-geth calls CalcDAFootprint only under `IsJovian`
    // (block_validator.go:120) and CalcDAFootprint is documented "must not be called for pre-Jovian
    // blocks" (rollup_cost.go:562). `has_da_footprint` is true iff the active config is Jovian
    // (OpForkSchedule.cpp).
    if (!cfg.has_da_footprint)
        return;
    // The empty-block and non-deposit-first-tx cases are rejected by processOpBlock's own checks
    // (and by op-geth's `len(txs) == 0 || !txs[0].IsDepositTx()` guard). Return here rather than
    // duplicate that verdict, so this function's contract is exactly "the Jovian attributes shape".
    if (txs.empty())
        return;
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr)
        return;

    const auto& data = firstDep->data;
    if (data.size() == IsthmusL1AttributesLen)
    {
        // Jovian *activation* block. The first Jovian block still carries Isthmus-length L1
        // attributes (no DA-footprint gas scalar yet) and op-geth requires it to be deposits-only
        // (rollup_cost.go:568-576). Checking the LAST tx is sufficient because deposits always
        // precede non-deposits (enforced by processOpBlock's "deposit after non-deposit" guard),
        // exactly op-geth's own justification.
        if (!std::holds_alternative<DepositTx>(txs.back().tx))
            throw std::runtime_error(
                "op block: unexpected non-deposit transactions in Jovian activation block");
        return;
    }

    // A normal Jovian block's L1 attributes calldata must carry the Jovian selector and be at
    // least the Jovian length (op-geth `ExtractDAFootprintGasScalar`, rollup_cost.go:547-556).
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
        // std::runtime_error (NOT invalid_argument): the scheduler's catch ladder treats the
        // logic_error family as a local fault (-32603) but runtime_error as a block-level
        // rejection (INVALID); a Prague-request block on an OP chain is the latter.
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
    // Step 1: pre-block system call (4788/2935; revision-gating and silent skip on missing code
    // are both handled inside evmone).
    applyDiff(bcos::evm::sanitizeStateDiff(
        view, evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)));

    // Step 2 precondition: the first tx must be the L1 attributes deposit (stricter-than-spec).
    if (txs.empty())
        throw std::runtime_error("op block: missing L1 attributes deposit (empty block)");
    const auto* firstDep = std::get_if<DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !isL1AttributesTx(*firstDep))
        throw std::runtime_error("op block: first tx is not the L1 attributes deposit");

    // Step 2 precondition: Jovian L1-attributes block shape — attributes selector/length and the
    // activation block's deposits-only rule. No-op pre-Jovian. Placed before the
    // per-tx loop so an activation block carrying a user tx is refused before any of it executes,
    // mirroring op-geth's `CalcDAFootprint` in block validation.
    validateJovianBlockShape(txs, cfg);

    OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    OpFeeParams fee{};

    for (const auto& btx : txs)
    {
        if (const auto* dep = std::get_if<DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw std::runtime_error("op block: deposit after non-deposit tx");
            evmone::state::StateDiff diff;
            auto receipt = runDeposit(
                view, block, hashes, *dep, cfg, vm, chainId, blockGasLeft, receiptFactory, diff);
            applyDiff(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(kDepositTxType));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                // Step 2: fee params are read from the storage slot values after this block's
                // attributes tx executes; deferred lazily to the first normal tx, equivalent to
                // op-geth's per-block cache (rollup_cost.go:162-164/:199-207).
                fee = loadOpFeeParams(view);
                // The Jovian DA footprint gas scalar's authoritative source is the first L1
                // attributes deposit's calldata[176:178] (op-geth ExtractDAFootprintGasScalar,
                // rollup_cost.go:555), not L1Block slot8: if the attributes deposit fails, the EVM
                // rolls back the storage write so slot8 keeps the previous block's stale value,
                // while calldata always carries this block's correct value. An activation block
                // (data.size()==176) forces 0 (op-geth CalcDAFootprint:571-577); a normal block
                // (data.size()>=178) reads the fixed offset [176:178] (not len-2).
                if (cfg.has_da_footprint)
                {
                    const auto& attrData = std::get<DepositTx>(txs[0].tx).data;
                    if (attrData.size() == IsthmusL1AttributesLen)
                    {
                        // The activation block has no user tx, so this branch is structurally
                        // unreachable; if the deposits-only rule were ever relaxed, op-geth would
                        // reject the block here (CalcDAFootprint requires deposits-only for
                        // Isthmus length, rollup_cost.go:572-575) rather than set 0.
                        fee.da_footprint_gas_scalar = 0;
                    }
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
            auto v = opValidate(view, block, tx, env, cfg, fee, blockGasLeft);
            if (const auto* err = std::get_if<std::error_code>(&v))
                // op-geth: a normal tx that fails validation has no failed-receipt mechanism;
                // Process voids the whole block outright (state_transition preCheck →
                // state_processor.go:109-113).
                throw std::runtime_error("op block: invalid non-deposit tx: " + err->message());
            // L1/DA/operator cost were derived from `env` in opValidate and frozen into props;
            // opTransition charges from that same snapshot (no second fee read, no re-encoding).
            evmone::state::StateDiff diff;
            auto receipt = opTransition(view, block, hashes, tx, cfg, vm,
                std::get<OpTxProperties>(v), chainId, receiptFactory, diff);
            applyDiff(diff);
            const auto gasUsed = narrowGasUsed(receipt->gasUsed());
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(hexCumulative(cumulative));
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(tx.type));
        }
    }

    // Step 4: end-of-block finalize.
    result.finalizeDiff = finalizeOpBlock(view, cfg, block.coinbase);
    applyDiff(result.finalizeDiff);

    result.gasUsed = cumulative;
    return result;
}

// ---- block-header seal (merged from OpBlockSeal.cpp) ----
namespace
{
/// Parse the receipt's cumulativeGasUsed string (set by processOpBlock as "0x" + lowercase hex,
/// op-geth hexutil.Uint64 convention) back to the exact uint64 the EncodeIndex leaf needs.
[[nodiscard]] uint64_t parseHexUint64(std::string_view s)
{
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s.remove_prefix(2);
    uint64_t v = 0;
    for (const char c : s)
    {
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= static_cast<uint8_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= static_cast<uint8_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= static_cast<uint8_t>(c - 'A' + 10);
        else
            throw std::runtime_error(
                "op block: invalid cumulativeGasUsed in receipt (not hex): " + std::string(s));
    }
    return v;
}

/// Appends the RLP list of logs: for each log, [address(20B), [topics...], data]; the whole
/// collection is itself wrapped in a list. Byte-identical to evmone's rlp::encode_container over
/// std::vector<Log> (rlp.hpp encode_container + rlp_encode(Log) = encode_tuple(addr, topics,
/// data)); rebuilt from bcos::protocol::LogEntry (the same raw bytes mapOpLogs stored).
inline void encodeLogsList(bcos::bytes& to, gsl::span<const bcos::protocol::LogEntry> logs)
{
    bcos::bytes content;
    for (const auto& log : logs)
    {
        bcos::bytes entry;
        // address (raw 20 bytes, LogEntry::address() reinterprets as binary, not hex)
        bcos::codec::rlp::encode(entry, log.address());
        // topics list
        bcos::bytes topics;
        for (const auto& topic : log.topics())
            bcos::codec::rlp::encode(topics, topic);  // h256 -> 32-byte string
        bcos::codec::rlp::encodeHeader(entry, {.isList = true, .payloadLength = topics.size()});
        entry.insert(entry.end(), topics.begin(), topics.end());
        // data
        bcos::codec::rlp::encode(entry, log.data());
        // wrap one log entry as a list
        bcos::codec::rlp::encodeHeader(content, {.isList = true, .payloadLength = entry.size()});
        content.insert(content.end(), entry.begin(), entry.end());
    }
    // wrap the log collection as a list
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = content.size()});
    to.insert(to.end(), content.begin(), content.end());
}
}  // namespace

evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    // Secure-trie over one account's live slot map, built with FISCO computeTrieRoot (same
    // construction as the retired evmone mpt_hash.cpp:13-24: key = keccak256(slot), value =
    // rlp(trimmed value)). Defensive continue on zero values.
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
        // op-geth's storage-trie leaf value is rlp(trimmed value) (trie/secure_trie.go
        // UpdateStorage: v,_ := rlp.EncodeToBytes(value) enters the trie), i.e. a SECOND RLP
        // wrapping of the trimmed bytes. The old implementation used the raw trimmed bytes as the
        // leaf value, producing d00be84d... instead of op-geth's 02dffd0c... on single-slot
        // accounts (exposed by the L2 message_passer_write case). Aligned with accountStorageRoot
        // (adapter/StateRootCompute.cpp:21-22): rlp-encode the trimmed value into the leaf first.
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
    // FISCO 0 == success. RLP bool semantics (op-geth statusEncoding): true → 0x01, false → 0x80
    // (empty string). Pushed manually because bcos::codec::rlp::encode's UnsignedByte path
    // instantiates toCompactBigEndian(bool|byte), which is either shift-overflow or
    // -Wundefined-inline; the value space is exactly {0x01, 0x80} so a raw push is equivalent.
    const bool success = (r.status() == 0);
    const uint64_t cumGas = parseHexUint64(r.cumulativeGasUsed());
    const auto bloom = r.logsBloom();
    const auto logs = r.logEntries();

    // payload = rlp([status, cumGas, bloom, logs]) + (deposit) [nonce, version]
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

    // receipts-root: key = rlp(index), leaf = EncodeIndex-semantics encoding. Built with FISCO
    // computeTrieRootVarKey (variable-length keys rlp(0..N), ascending, non-prefix-of-each-other —
    // the same construction as the retired evmone list-trie mpt_hash.cpp:38-46).
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

    // Block-level logsBloom: bitwise-OR each receipt's 256-byte bloom (the FISCO receipt carries
    // its own logsBloom, set by the execution layer — same projection the evmone span overload
    // in bloom_filter.cpp:46-53 produces).
    for (const auto& r : result.receipts)
    {
        const auto bloom = r->logsBloom();
        for (size_t i = 0; i < 256 && i < bloom.size(); ++i)
            seal.logsBloom.bytes[i] |= bloom[i];
    }

    // withdrawalsRoot / requestsHash: semantics switch starting at Isthmus.
    if (cfg.fork >= OpFork::Isthmus)
    {
        seal.withdrawalsRoot = opStorageRoot(messagePasserStorage);
        seal.requestsHash = OP_EMPTY_REQUESTS_HASH;
    }
    else
    {
        // Canyon+ withdrawals list is always empty → empty-trie root; the requests header field
        // does not exist in the CANCUN family. FISCO emptyRootHash() == keccak256(RLP("")).
        auto const emptyRoot = bcos::ledger::mpt::emptyRootHash();
        std::memcpy(
            seal.withdrawalsRoot.bytes, emptyRoot.data(), sizeof(seal.withdrawalsRoot.bytes));
    }

    // Jovian block-header BlobGasUsed reuse slot (equivalent to CalcDAFootprint, see header
    // comment). Only non-deposit receipts carry
    // da_footprint in their opStackMeta — deposits always have it nullopt, so summing over every
    // receipt is equivalent to the old OpTxReceipt-only loop.
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
