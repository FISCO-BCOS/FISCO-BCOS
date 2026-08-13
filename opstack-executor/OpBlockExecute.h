#pragma once

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <ethereum-executor/BCOS2Evmone.h>
#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpErrors.h>
#include <opstack-executor/OpRlpDecode.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
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

/// Block execution result. txTypes[i] is the EIP-2718 type byte for receipts[i] (the FISCO
/// receipt has no tx-type slot; sealOpBlock's EncodeIndex leaf needs it).
struct OpBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    std::vector<uint8_t> txTypes;
    int64_t gasUsed = 0;
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output
};

/// Execute a whole block (system_call → L1 deposit → fee → per-tx → finalize). **Discard-writes
/// contract**: on any throw the caller must discard all writes already applied (op-geth Process
/// semantics). Throws std::runtime_error on block-level errors.
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);

// ---- Jovian L1-attributes block shape ----
// The L1-attributes deposit's calldata is 176B on the Jovian activation block, 178B with the
// Jovian selector thereafter.
inline constexpr std::size_t IsthmusL1AttributesLen = 176;
inline constexpr std::size_t JovianL1AttributesLen = 178;
inline constexpr std::array<uint8_t, 4> JovianL1AttributesSelector = {0x3d, 0xb6, 0xbe, 0x2b};

/// Validate the Jovian L1-attributes block shape (selector/length + activation deposits-only).
/// No-op pre-Jovian. Throws std::runtime_error.
void validateJovianBlockShape(std::span<const OpBlockTx> txs, const OpForkConfig& cfg);

// ---- shared per-receipt helpers (one implementation shared with runOpBlockInjection) ----

/// Stricter-than-spec content check for the L1 attributes deposit (to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR); rejects hand-crafted payloads.
[[nodiscard]] inline bool isL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// Bounds-checked u256→int64 narrowing (a corrupt receipt must not wrap the gas pool).
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw std::runtime_error("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// "0x" + lowercase hex (op-geth hexutil.Uint64); parsed back by encodeReceiptForRoot.
[[nodiscard]] inline std::string hexCumulative(uint64_t cumulative)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << cumulative;
    return oss.str();
}

/// Block finalize: no ommers / block reward; Prague requests suppressed (false throws).
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- seal: header commitment functions (OpBlockSeal struct lives in OpErrors.h) ----
using evmc::literals::operator""_bytes32;

/// Isthmus+ requestsHash = sha256("").
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Single-account storage root (secure trie: key = keccak256(slot), value = rlp(trimmed)).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the header commitments; messagePasserStorage = post-finalize MessagePasser snapshot.
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);

/// Receipts-root leaf, byte-for-byte op-geth `Receipts.EncodeIndex` semantics:
/// deposit 0x7E || rlp([status, cumGas, bloom, logs, nonce, version]);
/// normal  typed prefix + rlp([status, cumGas, bloom, logs]).
[[nodiscard]] evmc::bytes encodeReceiptForRoot(
    const bcos::protocol::TransactionReceipt& r, uint8_t txType);
}  // namespace bcos::evm::opstack

// ---- block registration + execution ----

namespace bcos::evm::engine
{
namespace detail
{
/// Decode a 0x7E deposit envelope into a DepositTx. DepositTx field order:
/// [sourceHash, from, to, mint, value, gas, isSystemTransaction, data] — no signature (`from` is
/// explicit). Envelope 0x7E || rlp([...]); body starts at the list header.
inline bcos::evm::opstack::DepositTx decodeDepositTx(bcos::bytes rawEntry)
{
    // Envelope 0x7E || rlp([...]); body starts at the list header.
    bcos::bytesRef body(rawEntry.data() + 1, rawEntry.size() - 1);
    auto listBody = enterList(body);
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = decodeHashField(listBody);
    dep.from = decodeAddressField(listBody);
    dep.to = decodeOptionalAddressField(listBody);
    // mint/value nilability: nil and a present-but-zero big.Int are RLP-indistinguishable (both
    // encode to the empty string), so decode as a plain scalar defaulting to 0.
    dep.mint = decodeU256Scalar(listBody);
    dep.value = decodeU256Scalar(listBody);
    dep.gas_limit = narrowGasLimit(decodeU64Scalar(listBody), "deposit.gas");
    dep.is_system_tx = decodeBoolField(listBody);
    dep.data = decodeBytesField(listBody);
    expectExhausted(listBody, "deposit envelope fields");
    expectExhausted(body, "deposit envelope (trailing bytes after the field list)");
    return dep;
}
}  // namespace detail

// Forward-declared; defined at the end of this block.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes);

// ---- block execution: per-transaction injection loop ----

/// Per-tx injection loop replicating processOpBlock's orchestration through OpstackExecutor.
/// The block's `transactions` (block-order Transaction objects, already converted from the raw
/// EIP-2718 envelopes by the caller — opEnvelopeToTars lives in the engine lib; building it here
/// would create a link cycle) and `deposits` (decoded 0x7E deposit envelopes) are consumed
/// directly; the raw type byte per rawTxBytes[i][0] dispatches deposit vs normal. poison/hashErr →
/// OpStorageError, shape/validation → OpConsensusError.
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
                    "op block: L1 attributes transaction data too short for DA footprint gas "
                    "scalar");
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
        if (rawTxBytes[i].empty())  // 空 envelope：直接 raw[0] 会越界；旧 decodeOneRawTx 的干净
                                    // INVALID 保留
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
                throw OpConsensusError(
                    "op block: unsupported tx type byte 0x" + std::to_string(rawTxBytes[i][0]));
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
                receipt = bcos::task::syncWait(
                    executor.executeTransaction(view, header, *transactions[i], /*contextID=*/0,
                        ledgerConfig, /*call=*/false, fee, blockGasLeft, chainId, &hashes));
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
/// of mismatchedFieldOf).
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

/// transactionsRoot over raw EIP-2718 envelopes (trie key = rlp(index), value = raw wire bytes).
/// Matches op-geth's DeriveSha because the raw-tx decoders reject non-canonical encodings
/// (assertCanonicalRoundTrip fails closed if that lapses). Two call sites: the engine's
/// pre-execution blockHash check and runOpBlockInjection's txRoot.
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
