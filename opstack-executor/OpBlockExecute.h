#pragma once

#include <bcos-codec/rlp/RLPEncode.h>            // rlp::encode (computeOpTxRoot)
#include <bcos-concepts/ByteBuffer.h>            // bytebuffer::toView (opstackRegisterBlock)
#include <bcos-evm/adapter/StateDiffSanitize.h>  // sanitizeStateDiff (runOpBlockInjection)
#include <bcos-evm/adapter/StateRootCompute.h>   // stateRootOf (runOpBlockInjection)
#include <bcos-evm/opstack/OpFeeParams.h>        // loadOpFeeParams / OpFeeParams
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/engine/Errors.h>  // OpExecutionInternalError
#include <bcos-framework/engine/Types.h>   // ExecutionPayload (announcedCommitmentsOf)
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>  // SYS_* table constants
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>  // storage2::writeOne
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/mpt/HashBuilder.h>                  // computeTrieRootVarKey (computeOpTxRoot)
#include <bcos-tars-protocol/protocol/TransactionImpl.h>  // bcostars::Transaction
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <ethereum-executor/BCOS2Evmone.h>  // applyStateDiff
#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpErrors.h>         // OpBlockSeal / OpBlockCommitments / conversions
#include <opstack-executor/OpRlpDecode.h>      // toBlockInfo / narrowU256ToU64 / toEvmcBytes32
#include <opstack-executor/OpstackExecutor.h>  // runOpBlockInjection's injection entries
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>  // runOpBlockInjection's state bridge
#include <boost/lexical_cast.hpp>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>  // system_call_block_start
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::opstack
{
/// One transaction within a block: deposit or normal tx (a normal tx must carry a signed envelope
/// for L1 fee calculation).
struct OpBlockTx
{
    std::variant<DepositTx, evmone::state::Transaction> tx;
    evmc::bytes signedEnvelope;  // empty for deposit
};

/// Block execution result. Receipts keep block order; txTypes[i] is the EIP-2718 type byte for
/// receipts[i] (the FISCO receipt has no tx-type slot; sealOpBlock's EncodeIndex leaf needs it).
struct OpBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    std::vector<uint8_t> txTypes;           // one EIP-2718 type byte per receipt, same order
    int64_t gasUsed = 0;                    // = last tx's cumulative
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output (already delivered via
                                            // applyDiff)
};

/// Execute a whole block: system_call_block_start → L1 attributes deposit → loadOpFeeParams →
/// per-tx (gas pool / cumulative / write-back) → finalizeOpBlock. **Discard-writes contract**: on
/// any throw the caller must discard all writes already applied (op-geth Process semantics).
/// Throws std::runtime_error (block-level) on empty/ill-formed blocks, gas overruns, or a
/// validate error on a normal tx.
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);

// ---- Jovian L1-attributes block shape (op-geth rollup_cost.go) ----
// The L1-attributes deposit's calldata is 176B (IsthmusL1AttributesLen) on the Jovian activation
// block and 178B with JovianL1AttributesSelector thereafter.
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Validate the Jovian L1-attributes block shape (C-3 selector/length, C-4 activation
/// deposits-only). No-op pre-Jovian. Mirrors the validation half of op-geth CalcDAFootprint;
/// throws std::runtime_error (block-level).
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg);

// ---- shared per-receipt helpers (one implementation shared with runOpBlockInjection) ----

/// Stricter-than-spec content check for the L1 attributes deposit (to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR). op-geth pushes this to the CL; kept here to reject hand-crafted payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Narrow receipt gasUsed (u256) to int64 with bounds check (a corrupt receipt must not wrap the
/// gas pool).
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw std::runtime_error("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// "0x" + lowercase hex (op-geth hexutil.Uint64). Parsed back by encodeReceiptForRoot.
[[nodiscard]] inline std::string hexCumulative(uint64_t cumulative)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << cumulative;
    return oss.str();
}

/// Block finalize: no ommers / block reward; Prague requests suppressed
/// (cfg.disable_prague_requests is always true for OP; false throws runtime_error). The
/// EIP-4788/2935 pre-execution system call is a separate orchestration step, not part of this
/// function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- seal: header commitment functions (OpBlockSeal struct lives in OpErrors.h) ----
using evmc::literals::operator""_bytes32;

/// Isthmus+ requestsHash = sha256("") (op-geth EmptyRequestsHash).
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Single-account storage root (secure trie: key = keccak256(slot), value = rlp(trimmed));
/// reproduced from evmone's private mpt_hash.cpp helper.
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the header commitment fields. messagePasserStorage = the post-finalize storage snapshot
/// of OP_L2_TO_L1_MESSAGE_PASSER (op-geth takes it after IntermediateRoot); pre-Isthmus forks use
/// the empty-list withdrawals root.
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// Receipts-root leaf: byte-for-byte op-geth `Receipts.EncodeIndex` semantics.
///   deposit: 0x7E || rlp([status, cumGas, bloom, logs, nonce, version])
///   normal:  typed prefix + rlp([status, cumGas, bloom, logs])
/// status is bool (FISCO 0 == success); cumGas from the hex string; bloom 256 bytes.
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack

// ---- block registration + execution ----

namespace bcos::evm::engine
{
// Defined at the end of this block — forward-declared because runOpBlockInjection calls it.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes);

/// raw EIP-2718 envelope -> tars Transaction (the engine's opEnvelopeToTars, injected by the
/// composition root — opstack-executor does not link engine).
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;

/// Write the block tables (OP prewriteBlockToBuffer): SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER /
/// SYS_NUMBER_2_BLOCK_HEADER / SYS_HASH_2_RECEIPT / SYS_HASH_2_TX + the eth read-path tables.
/// Receipt-count mismatch / null receipt -> OpExecutionInternalError (internal fault, not a block
/// verdict). blockHash comes from the caller (already validated), not recomputed here.
template <class ViewType>
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash,
    std::vector<bcos::bytes> const& rawTxBytes, OpExecuteBlockResult const& result,
    bcos::protocol::BlockFactory& blockFactory, EnvelopeToTarsConverter const& envelopeToTars)
{
    const auto blockNumberStr = boost::lexical_cast<std::string>(header.number());

    bcos::storage::Entry numberToHashEntry;
    numberToHashEntry.set(blockHash.asBytes());
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr},
        std::move(numberToHashEntry));

    bcos::storage::Entry hashToNumberEntry;
    hashToNumberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(hashToNumberEntry));

    // OP header -> standard s_number_2_header (never call header.hash(): empty dataHash throws).
    bcos::storage::Entry headerEntry;
    bcos::bytes headerBuffer;
    header.encode(headerBuffer);
    headerEntry.set(std::move(headerBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));

    // The eth RPC read path needs 2 more tables: SYS_CURRENT_STATE/SYS_KEY_CURRENT_NUMBER
    // (eth_blockNumber) and SYS_NUMBER_2_TXS (tx metadata for getBlockData); without them a VALID
    // block is unqueryable.
    bcos::storage::Entry numberEntry;
    numberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(numberEntry));

    auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
    // Metadata hashes use hashImpl.hash(rawEnvelope) — the same source as the SYS_HASH_2_* keys.
    // Rows whose envelopeToTars fails are skipped (consistent with the tx table).
    auto transactionsBlock = blockFactory.createBlock();
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        const auto txHash = hashImpl.hash(rawTxBytes[index]);
        std::string txTo;
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            txTo = std::string(txImpl.to());
        }
        auto txMetaData = blockFactory.createTransactionMetaData(txHash, std::move(txTo));
        transactionsBlock->appendTransactionMetaData(std::move(txMetaData));
    }
    bcos::bytes transactionsBuffer;
    transactionsBlock->encode(transactionsBuffer);
    bcos::storage::Entry number2TxEntry;
    number2TxEntry.set(std::move(transactionsBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr},
        std::move(number2TxEntry));

    // One receipt per tx is a broken-execution invariant.
    if (rawTxBytes.size() != result.receipts.size())
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "OP block execution returned a receipt count differing from "
                                  "the transaction count"});
    }
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        auto const& receipt = result.receipts[index];
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a null receipt"});
        }
        bcos::bytes encodedReceipt;
        receipt->encode(encodedReceipt);
        const auto txHash = hashImpl.hash(rawTxBytes[index]);

        bcos::storage::Entry receiptEntry;
        receiptEntry.set(std::move(encodedReceipt));
        co_await bcos::storage2::writeOne(view,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)},
            std::move(receiptEntry));

        // Conversion failure -> skip the row (the tx stays valid but unqueryable by hash).
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            bcos::bytes encodedTx;
            txImpl.encode(encodedTx);
            bcos::storage::Entry txEntry;
            txEntry.set(std::move(encodedTx));
            co_await bcos::storage2::writeOne(view,
                bcos::executor_v1::StateKey{
                    bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)},
                std::move(txEntry));
        }
    }
}

// ---- block execution: route B per-transaction injection loop ----

/// Route B: per-tx injection loop replicating processOpBlock's orchestration through
/// OpstackExecutor (system_call_block_start → deposit-first → lazy fee → per-tx gas/cumulative →
/// finalizeBlock). normalTxs are pre-built by the caller (opEnvelopeToTars lives in the engine
/// lib; building it here would create a link cycle). poison/hashErr → OpStorageError,
/// shape/validation → OpConsensusError.
template <class Storage>
OpExecuteBlockResult runOpBlockInjection(bcos::executor_v1::opstack::OpstackExecutor& executor,
    Storage& view, bcos::protocol::BlockHeader const& header,
    std::span<bcos::evm::opstack::OpBlockTx const> txs,
    std::span<bcos::protocol::Transaction::Ptr const> normalTxs,
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

    // (1) Pre-block system call (evmone called directly; no executor entry).
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first content check + Jovian shape (shared isL1AttributesTx — no copy drift).
    if (txs.empty())
        throw OpConsensusError("op block: missing L1 attributes deposit (empty block)");
    auto const* firstDep = std::get_if<op::DepositTx>(&txs[0].tx);
    if (firstDep == nullptr || !op::isL1AttributesTx(*firstDep))
        throw OpConsensusError("op block: first tx is not the L1 attributes deposit");
    op::validateJovianBlockShape(txs, cfg);

    op::OpBlockResult result;
    result.receipts.reserve(txs.size());
    int64_t blockGasLeft = blk.gas_limit;
    int64_t cumulative = 0;
    bool seenNonDeposit = false;
    bool feeLoaded = false;
    std::size_t normalIdx = 0;  // consume caller-prebuilt normalTxs
    op::OpFeeParams fee{};
    for (std::size_t i = 0; i < txs.size(); ++i)
    {
        auto const& btx = txs[i];
        if (auto const* dep = std::get_if<op::DepositTx>(&btx.tx))
        {
            if (seenNonDeposit)
                throw OpConsensusError("op block: deposit after non-deposit tx");
            auto receipt = bcos::task::syncWait(executor.executeDeposit(
                view, header, *dep, chainId, blockGasLeft, ledgerConfig, &hashes));
            auto const gasUsed =
                op::narrowGasUsed(receipt->gasUsed());  // op::-qualified
                                                        // (exported to ns bcos::evm::opstack)
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // op::-qualified
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(op::kDepositTxType));
        }
        else
        {
            seenNonDeposit = true;
            if (!feeLoaded)
            {
                fee = op::loadOpFeeParams(stateView);
                if (cfg.has_da_footprint)
                {
                    auto const& attrData = std::get<op::DepositTx>(txs[0].tx).data;
                    if (attrData.size() == op::IsthmusL1AttributesLen)
                        fee.da_footprint_gas_scalar = 0;
                    else if (attrData.size() >= op::JovianL1AttributesLen)
                        fee.da_footprint_gas_scalar = static_cast<uint16_t>(
                            (static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 2]) << 8) |
                            static_cast<uint16_t>(attrData[op::JovianL1AttributesLen - 1]));
                }
                feeLoaded = true;
            }
            auto const& tx = std::get<evmone::state::Transaction>(btx.tx);
            // normalTxs[i] pre-built by the caller (full envelope already overwritten). normalIdx
            // has no guard; a short caller vector would OOB.
            if (normalIdx >= normalTxs.size())
                throw OpConsensusError(
                    "runOpBlockInjection: normalTxs exhausted (caller-provided "
                    "normal txs mismatch block txs)");
            // Validation failure → OpConsensusError (INVALID), not -32603 — same channel as
            // processOpBlock's validate-error.
            protocol::TransactionReceipt::Ptr receipt;
            try
            {
                receipt = bcos::task::syncWait(executor.executeTransaction(view, header,
                    *normalTxs[normalIdx++], /*contextID=*/0, ledgerConfig,
                    /*call=*/false, fee, blockGasLeft, chainId, &hashes));
            }
            catch (const bcos::executor_v1::opstack::OpTxValidationFailed& e)
            {
                throw OpConsensusError(
                    "runOpBlockInjection: normal tx validation failed: " + std::string(e.what()));
            }
            auto const gasUsed = op::narrowGasUsed(receipt->gasUsed());  // op::-qualified
            blockGasLeft -= gasUsed;
            cumulative += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(cumulative));  // op::-qualified
            result.receipts.emplace_back(std::move(receipt));
            result.txTypes.emplace_back(static_cast<uint8_t>(tx.type));
        }
    }
    bcos::task::syncWait(executor.finalizeBlock(view, header, ledgerConfig));
    result.gasUsed = cumulative;
    // Storage-layer failure (block-hash lookup / poison flag) → OpStorageError (-32603), not
    // INVALID.
    if (hashErr.has_value())
        throw OpStorageError("runOpBlockInjection: block-hash lookup failed: " + *hashErr);

    // (4) commitments: MessagePasser snapshot → seal → stateRoot → txRoot.
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

/// Project the payload/header announced commitments into OpBlockCommitments (the "announced" side
/// of mismatchedFieldOf): 5 fields from ExecutionPayload, txRoot from computeTxRoot, blobGasUsed
/// reverse-narrowed, requestsHash from the rebuilt header.
inline OpBlockCommitments announcedCommitmentsOf(const bcos::engine::ExecutionPayload& payload,
    const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)
{
    OpBlockCommitments out{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = payloadBloomToH2048(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = transactionsRoot,
        .blobGasUsed = payload.blobGasUsed.has_value() ?
                           std::optional<uint64_t>(bcos::evm::engine::detail::narrowU256ToU64(
                               *payload.blobGasUsed, "ExecutionPayload.blobGasUsed")) :
                           std::nullopt,
        .requestsHash = ethHeader.requestsHash(),
    };
    return out;
}

/// transactionsRoot over the block's raw EIP-2718 envelopes: trie key = rlp(index), value = the
/// raw wire bytes as received. NOT op-geth's DeriveSha (which re-encodes from the parsed struct);
/// the two coincide because the raw-tx decoders reject every non-canonical encoding (the
/// assertCanonicalRoundTrip backstop fails closed if that ever lapses). One function, two call
/// sites: the engine's pre-execution blockHash check and runOpBlockInjection's txRoot.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    std::vector<std::pair<bcos::bytes, bcos::bytes>> entries;
    entries.reserve(rawTxBytes.size());
    uint64_t index = 0;
    for (auto const& rawItem : rawTxBytes)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, index);
        entries.emplace_back(std::move(key), bcos::bytes(std::begin(rawItem), std::end(rawItem)));
        ++index;
    }
    return bcos::ledger::mpt::computeTrieRootVarKey(entries).root;
}
}  // namespace bcos::evm::engine
