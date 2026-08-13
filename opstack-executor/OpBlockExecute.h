#pragma once

#include <bcos-concepts/ByteBuffer.h>            // bytebuffer::toView (opstackRegisterBlock)
#include <bcos-evm/adapter/StateDiffSanitize.h>  // sanitizeStateDiff (runOpBlockInjection)
#include <bcos-evm/adapter/StateRootCompute.h>   // stateRootOf (runOpBlockInjection)
#include <bcos-evm/opstack/OpFeeParams.h>        // loadOpFeeParams / OpFeeParams
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/engine/Errors.h>  // OpExecutionInternalError
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>  // SYS_* table constants
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>  // storage2::writeOne
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>  // bcostars::Transaction
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <ethereum-executor/BCOS2Evmone.h>  // applyStateDiff
#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpEngineSeam.h>     // computeOpTxRoot / toBcosH256
#include <opstack-executor/OpErrors.h>         // OpBlockSeal / OpExecuteBlockResult / errors
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

/// Block execution result. receipts keep their original in-block order (the receipts-root /
/// block-level bloom depend on this order; cumulative_gas_used is already filled in interleaved
/// order). Each receipt is a bcos::protocol::TransactionReceipt directly produced by the execution
/// layer — the OP metadata (l1/operator/DA, or deposit_nonce/version) rides in
/// its opStackMeta, so no evmone receipt wrapper survives here. txTypes[i] carries the EIP-2718
/// type byte that produced receipts[i] (kDepositTxType for deposits, else the Transaction::Type
/// value): the FISCO receipt interface has no tx-type slot, and sealOpBlock's EncodeIndex
/// receipts-root leaf needs the typed prefix (op-geth Receipts.EncodeIndex semantics).
struct OpBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    std::vector<uint8_t> txTypes;           // one EIP-2718 type byte per receipt, same order
    int64_t gasUsed = 0;                    // = last tx's cumulative
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output (already delivered via
                                            // applyDiff)
};

/// Execute a whole block (execution ordering): system_call_block_start → first L1 attributes
/// deposit → loadOpFeeParams → per-transaction (gas pool / cumulative / per-transaction write-back)
/// → finalizeOpBlock. Write-back callback: invoked immediately after each diff segment is produced;
/// the view read by the next step must already reflect it.
/// **discard-writes contract**: after any throw from this function, the caller must discard the
/// entire write set already applied via applyDiff within this block (same semantics as op-geth
/// Process discarding the whole statedb on error, state_processor.go:109-113). Throws
/// std::runtime_error (block-level error): txs empty or first tx is not the L1 attributes deposit
/// (to==OP_L1_BLOCK && from==OP_DEPOSITOR, stricter-than-spec); a deposit appears after a
/// non-deposit (stricter-than-spec); any tx gasLimit exceeds the remaining block gas; is_system_tx;
/// any validate error on a normal tx (op-geth has no failed-receipt mechanism for normal txs,
/// state_processor.go:109-113).
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);

// ---- Jovian L1-attributes block shape ----
// op-geth pins these in `core/types/rollup_cost.go`: the first (L1 attributes) deposit's calldata
// is `IsthmusL1AttributesLen` (176) bytes on the Jovian *activation* block (the DA-footprint gas
// scalar is not set yet) and `JovianL1AttributesLen` (178) bytes with `JovianL1AttributesSelector`
// (0x3db6be2b) thereafter (`rollup_cost.go:46-47/:65`).
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Validate the Jovian L1-attributes block shape (selector/length + activation deposits-only).
/// No-op for pre-Jovian configs (`cfg.has_da_footprint == false`) and for the
/// degenerate cases `processOpBlock` already rejects (empty block / non-deposit first tx), so it
/// is safe to call unconditionally at the top of `processOpBlock`. Mirrors the validation half of
/// op-geth `core/types/rollup_cost.go`'s `CalcDAFootprint` (`:563-591`); the footprint *sum* stays
/// on the seal side (OpBlockSeal.cpp). Throws `std::runtime_error` (block-level error) on a shape
/// violation, the same channel as `processOpBlock`'s sibling structural checks.
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg);

// ---- shared per-receipt helpers ----
// `narrowGasUsed` / `hexCumulative` / `isL1AttributesTx` were promoted out of OpBlockExecute.cpp's
// anonymous namespace (OpBlockExecute.cpp:16-49) so the per-transaction injector loop
// (runOpBlockInjection) and processOpBlock share ONE implementation — the
// "no copy drift" guard of the equivalence harness. Pure refactor: these three
// are only used inside OpBlockExecute.cpp (verified by grep), and the anonymous-namespace copies
// are deleted alongside, so no TU sees two definitions.

/// Stricter-than-spec: validate the L1 attributes deposit by content. op-geth's EL does not
/// perform this validation (pushed down to the CL layer); op-node always prepends a deposit
/// that satisfies both conditions, so a divergent verdict is only reachable via a hand-crafted
/// payload fed directly to engine_newPayload. Keep the check: it rejects malformed blocks
/// op-geth accepts, at zero cost on legitimate payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Narrow the FISCO receipt's gasUsed (u256) back to the int64 the gas pool/cumulative accounting
/// uses. The execution layer only ever stores a small positive gas_used, but the "widen -> check ->
/// narrow" discipline (Storage2State.h precedent) applies: a corrupt receipt must not silently
/// wrap blockGasLeft/cumulative.
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw std::runtime_error("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// "0x" + lowercase hex, no leading zeros (op-geth hexutil.Uint64 convention). Stored on the
/// receipt's cumulativeGasUsed string field; encodeReceiptForRoot parses it back to the exact
/// uint64 for the EncodeIndex leaf (see OpBlockSeal.cpp).
[[nodiscard]] inline std::string hexCumulative(uint64_t cumulative)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << cumulative;
    return oss.str();
}

/// OP block finalize: withdrawals are always empty, no ommers / block reward; EIP-6110/7002/7251
/// requests are suppressed per cfg.disable_prague_requests — op-geth explicitly disables them for
/// OP Isthmus (state_processor.go:140-156), and this switch is always true for every OP fork; false
/// throws std::runtime_error (block-level rejection, not a local fault). Scope note: on OP Isthmus
/// op-geth still runs the EIP-4788/2935 **pre-execution** system call (state_processor.go:90-95) —
/// that is a precondition step wired in during block-level orchestration, not part of this finalize
/// function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- block-header commitment fields + seal ----
// `OpBlockSeal` (the commitment struct) lives in OpErrors.h (OpExecuteBlockResult carries it by
// value); this section holds the seal *functions* + the requests-hash constant. The seal functions
// compute the commitment fields from the block execution result.
using evmc::literals::operator""_bytes32;

/// OP Isthmus+ block-header requestsHash is a fixed value = sha256("") (op-geth EmptyRequestsHash,
/// hashes.go:43-44; on the build side worker.go:283-290 calls CalcRequestsHash on an empty list, on
/// the validation side block_validator.go:177-184 always matches Process's nil requests).
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Single-account storage root (secure-trie: key = keccak256(slot), value = rlp(trim(value)),
/// zero-value slots skipped — aligned with the private helper in evmone mpt_hash.cpp:13-24; the
/// upstream does not export it, so it is reproduced here as an exported piece and registered in the
/// upstream-diff manifest to watch for drift).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the block-header commitment fields from the block execution result.
/// messagePasserStorage: the storage snapshot of OP_L2_TO_L1_MESSAGE_PASSER (0x4200…0016,
/// protocol_params.go:31) **after end-of-block finalize** (on the op-geth build side
/// consensus.go:416-427 takes it after IntermediateRoot — the timing point is a documented
/// contract; decision point 3 ruling (a): the caller provides it, keeping the StateView narrow
/// interface). pre-Isthmus forks ignore it (withdrawalsRoot = empty-list root EMPTY_MPT_HASH,
/// Canyon+ withdrawals list is always empty). Precondition: result.receipts is non-empty
/// (guaranteed by processOpBlock's first-attributes invariant; an empty sequence produces
/// EMPTY_MPT_HASH rather than an error).
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// receipts-root leaf encoding rebuilt from a bcos::protocol::TransactionReceipt (replaces the
/// former OpDepositReceipt/OpTxReceipt-based encoders). `txType` is the EIP-2718
/// type byte that produced the receipt (kDepositTxType for deposits, else the Transaction::Type
/// value) — the FISCO receipt interface has no tx-type slot, so the caller threads it through
/// OpBlockResult::txTypes. Byte-for-byte op-geth `Receipts.EncodeIndex` semantics
/// (receipt.go:568-592 — note this is NOT MarshalBinary :279-288; the two deliberately differ for
/// a receipt that "has nonce, has no version", and the function-header comment :564-567
/// explicitly forbids changing that):
///   deposit: 0x7E || rlp([status, cumulativeGasUsed, logsBloom, logs, depositNonce,
///   depositReceiptVersion]) — nonce/version read from opStackMeta (depositReceiptRLP :136-148).
///   normal tx: typed raw-byte prefix (empty for legacy) + rlp([status, cumGas, bloom, logs]),
///   byte-identical to EncodeIndex for type 0/1/2/4.
/// status is projected as bool (FISCO 0 == success); cumulativeGasUsed() is parsed from its hex
/// string; logsBloom() is the 256-byte bloom; logEntries() re-encode the evmone Log shape.
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack

// ---- block registration (merged from OpBlockRegister.h) ----

namespace bcos::evm::engine
{
/// raw EIP-2718 envelope -> tars Transaction. Signature matches the engine's
/// `bcos::engine::detail::opEnvelopeToTars`; the composition root (Initializer) injects a lambda
/// invoking it — opstack-executor does not link bcos-rpc / bcos-engine.
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;

/// Write the block tables — the OP equivalent of ledger::prewriteBlockToBuffer. Moved line by
/// line from the engine's registerOpBlock, with data sources switched to explicit parameters.
/// Five tables:
///   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
///   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
/// Error classification: receipt-count invariant / null receipt -> OpExecutionInternalError; write
/// failures propagate as-is (the engine barrier classifies -32603). blockHash is passed in
/// explicitly by the caller (already validated by engine step 2 == header.opHeaderHash(
/// opHeaderConst())); not recomputed here, avoiding constant drift.
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

    // The OP header lands in the standard s_number_2_header table as a tars BlockHeader (same
    // table/format as ordinary FISCO blocks). dataHash is empty -> header.hash() throws
    // EmptyBlockHeaderHash; this path never calls it. encode() is a `void encode(bytes&)` out-param
    // (BlockHeader.h:50) — build a buffer first, then read it.
    bcos::storage::Entry headerEntry;
    bcos::bytes headerBuffer;
    header.encode(headerBuffer);
    headerEntry.set(std::move(headerBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));

    // ---- legacy-ledger read-path parity (ethereum executor `Ledger::asyncPrewriteBlock`): ----
    // The OP path used to write only 5 tables; the eth RPC read path depends on two more, without
    // which a VALID block is unqueryable:
    //   1. SYS_CURRENT_STATE / SYS_KEY_CURRENT_NUMBER — eth_blockNumber reads it and returns 0
    //      (block committed but head not advanced); entry = blockNumber string, matching the
    //      production precedent Ledger.cpp:266-270.
    //   2. SYS_NUMBER_2_TXS — tx metadata (hash + to list, persisted via Block::encode);
    //      getBlockData (RECEIPTS/TRANSACTIONS/TRANSACTIONS_HASH) reads it first to get the tx
    //      hash list, then queries SYS_HASH_2_*; without it receipts/txs read empty and
    //      getBlockByNumber(1) returns null. Format matches Ledger.cpp:284-310:
    //      createBlock + appendTransactionMetaData(hash, to) + block.encode().
    // Both writes use storage2::writeOne(view, ...), landing in the same mergeView batch as the
    // OP tables, with identical key encoding.
    bcos::storage::Entry numberEntry;
    numberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(numberEntry));

    auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
    // Must use blockFactory.createBlock() (not any stateful object the blockFactory already
    // holds): appendTransactionMetaData expects an empty block to carry the metadata. The hash may
    // come from hashImpl (already computed) or the tars tx's hash(); this path uniformly uses
    // hashImpl.hash(rawEnvelope) — the same source as the SYS_HASH_2_TX / SYS_HASH_2_RECEIPT keys
    // (see the loop below), so the metadata's hash matches the lookup keys byte-for-byte. `to`
    // comes from the tars tx's to() (rows whose envelopeToTars fails are skipped, same
    // skip-on-conversion-failure semantics as SYS_HASH_2_TX, keeping metadata and the tx table
    // consistent).
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

    // processOpBlock produces exactly one receipt per tx; a count mismatch is a broken execution
    // invariant, failing loudly (an internal error, not a verdict on the block).
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

        // Conversion failure (malformed / un-enumerated envelope) -> skip the row; the block stays
        // VALID, that tx is just not queryable by hash.
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

// ---- block execution: route B per-transaction injection loop (merged from OpBlockInjector.h) ----

/// Per-transaction injection loop: replicates processOpBlock's orchestration
/// (system_call_block_start → deposit-first → lazy loadOpFeeParams + Jovian D-1 override →
/// per-tx blockGasLeft decrement + setCumulativeGasUsed → finalizeBlock), executed through the
/// OpstackExecutor injection-style entry points. Returns the same result shape as processOpBlock.
/// Normal txs' FISCO Transactions are pre-built by the caller (normalTxs maps 1:1 to
/// the non-deposit txs) — opEnvelopeToTars lives in the engine lib; building it here would create
/// a link cycle.
/// Error classification — poison/hashErr → OpStorageError, block shape/validation →
/// OpConsensusError.
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

    // (1) Pre-block system_call_block_start (no executor entry; evmone called directly).
    auto sysDiff =
        evmone::state::system_call_block_start(stateView, blk, hashes, cfg.rev, executor.vm());
    bcos::task::syncWait(eth::applyStateDiff(
        view, bcos::evm::sanitizeStateDiff(stateView, sysDiff), cfg.rev, *hashImpl));

    // (2) deposit-first content check + Jovian shape. Block-shape rejection → OpConsensusError.
    // Call the exported op::isL1AttributesTx (exported to OpBlockExecute.h) rather than inlining
    // a copy — a duplicate would drift from processOpBlock.
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
            // Normal txs are pre-built by the caller (normalTxs[i], whose extraTransactionBytes
            // is already the full envelope — takeToTarsTransaction stores the signing preimage
            // and must be overwritten). normalIdx has no upper-bound guard; a short caller vector
            // would OOB (BaselineScheduler consumes this in production).
            if (normalIdx >= normalTxs.size())
                throw OpConsensusError(
                    "runOpBlockInjection: normalTxs exhausted (caller-provided "
                    "normal txs mismatch block txs)");
            // Classification contract: validation failure (opValidate rejects a normal tx) →
            // OpConsensusError (INVALID), not -32603. OpTxValidationFailed only appeared in the
            // direct injector (the harness skips invalid_ vectors); on the engine delegate path,
            // misclassification would report INVALID vectors as -32603 (mapDelegateError throws
            // an internal error) — aligned with processOpBlock's validate-error channel
            // (runtime_error → OpConsensusError). syncWait rethrows synchronously; FISCO types
            // have stable typeinfo, so typed catch binds reliably.
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
}  // namespace bcos::evm::engine
