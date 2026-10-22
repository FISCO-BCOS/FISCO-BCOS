/// @file OpEthBlockExecute.h
/// @brief Block-level helpers of the bcos-evm-free OP execution layer, shared
///        by the production drivers (OpEthBlockSteps.h's preBlockOpEthSteps /
///        finalizeOpEthBlockResult, driven around OpEthExecutor +
///        SchedulerSerialImpl): the block-info projection
///        (buildOpEthBlockInfo), the Jovian L1-attributes shape rules, the
///        MessagePasser snapshot, the receipt type-byte classification and the
///        header-commitment seal (sealOpEthBlock) — the bcos-evm-free
///        counterparts of the pieces of OpBlockExecute.h's processOpBlock /
///        sealOpBlock, composed from the Part-A policy hooks and the B1-B6
///        layer (OpForkSpec / OpFeeParams / OpRollupCost / OpPolicy /
///        OpEthReceipt / OpEthDeposit).
///
/// Semantics are ported from OpBlockExecute.cpp (processOpBlock + sealOpBlock)
/// with the same fork gating; op-geth references live in those originals.

#pragma once

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/engine/Constants.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <ethereum-executor/EVMSupport.h>
#include <ethereum-executor/EthereumHost.h>
#include <ethereum-executor/EthereumState.h>
#include <opstack-executor/OpEthDeposit.h>
#include <opstack-executor/OpEthReceipt.h>
#include <opstack-executor/OpExecutionPolicy.h>
#include <opstack-executor/OpFeeParams.h>
#include <opstack-executor/OpForkSpec.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

/// Consensus-level fault raised by the helpers in this file. The block steps
/// (OpEthBlockSteps.h) catch it at the layer boundary and reclassify it to
/// bcos::evm::OpConsensusError (INVALID), keeping the message — the legacy
/// path's classification surface.
struct OpEthBlockError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// ---- Jovian L1-attributes block shape (mirrors OpBlockExecute.h's
// validateJovianL1AttributesShape / jovianDaFootprintGasScalar; the length/selector
// constants mirror OpTransition.h's IsthmusL1AttributesLen / JovianL1AttributesLen /
// JovianL1AttributesSelector — cross-checked in the fork-spec equivalence test). ----
inline constexpr std::size_t OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN = 176;
inline constexpr std::size_t OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN = 178;
inline constexpr std::array<uint8_t, 4> OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR = {
    0x3d, 0xb6, 0xbe, 0x2b};

/// Shared Jovian L1-attributes shape (selector/length + activation deposits-only).
/// No-op pre-Jovian. Throws OpEthBlockError.
inline void validateOpEthJovianShape(
    std::span<uint8_t const> data, bool lastTxIsDeposit, OpForkSpec const& spec)
{
    if (!spec.has_da_footprint)
        return;
    if (data.size() == OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN)
    {
        // Jovian activation block: Isthmus-length attributes, must be deposits-only
        // (op-geth rollup_cost.go:568-576). Checking the last tx suffices (deposits
        // always precede non-deposits).
        if (!lastTxIsDeposit)
            throw OpEthBlockError(
                "op-eth block: unexpected non-deposit transactions in Jovian activation block");
        return;
    }
    if (data.size() < OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN)
        throw OpEthBlockError(
            "op-eth block: L1 attributes transaction data too short for DA footprint gas scalar");
    if (!std::equal(OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.begin(),
            OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.end(), data.begin()))
        throw OpEthBlockError(
            "op-eth block: L1 attributes transaction data does not have Jovian selector");
}

/// Stricter-than-spec content check for the L1 attributes deposit (to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR) — the mirror of OpBlockExecute.h's isL1AttributesTx; a
/// mismatch is warn-only upstream (op-geth/op-reth accept a non-attributes first
/// deposit at validation).
[[nodiscard]] inline bool isOpEthL1AttributesTx(const DepositTx& dep) noexcept
{
    return dep.to.has_value() && *dep.to == OP_L1_BLOCK && dep.from == OP_DEPOSITOR;
}

/// DA footprint gas scalar from L1-attributes calldata: Isthmus 176B → 0; Jovian ≥178B →
/// big-endian uint16 at [176:178]. nullopt when the length is neither (shape validation
/// should already have rejected that case on a Jovian block).
[[nodiscard]] inline std::optional<uint16_t> opEthJovianDaFootprintGasScalar(
    std::span<uint8_t const> attrData)
{
    if (attrData.size() == OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN)
        return uint16_t{0};
    if (attrData.size() >= OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN)
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(attrData[OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN - 2]) << 8) |
            static_cast<uint16_t>(attrData[OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN - 1]));
    return std::nullopt;
}

/// EIP-2718 tx-type classification (the single home on the new layer — the
/// legacy OpCommon.h classifyTxType retired with the bcos-evm path in 3.5):
/// deposit 0x7e → itself; legacy (>= 0xc0 RLP list prefix) → 0; typed → its own
/// type byte. Unknown bytes pass through unchanged.
[[nodiscard]] constexpr uint8_t opEthClassifyTxType(uint8_t typeByte) noexcept
{
    if (typeByte == OP_DEPOSIT_TX_TYPE)
        return typeByte;
    if (typeByte >= 0xc0)
        return 0;
    return typeByte;
}

/// EthBlockInfo from the block header (the retired OpCommon.h toBlockInfo's
/// successor): OP takes gas_limit / base_fee / coinbase / prev_randao /
/// parentBeaconBlockRoot from the HEADER, not the ledger config (contrast
/// eth::buildBlockInfo, the L1 builder). Pre-Ecotone headers carry no baseFee/parentBeaconBlockRoot
/// fields — the zero-filled optionals are dead EVM inputs pre-Cancun (no EIP-4788 system call, and
/// OpPolicy::blobBaseFee ignores the block's blob fields), so the leniency is semantics-neutral;
/// Ecotone+ headers stay strict. blob_base_fee stays nullopt: OpPolicy::blobBaseFee asserts exactly
/// that (the OP BLOBBASEFEE opcode is the constant 1, never the L1 EIP-4844 price).
/// @p lenientOptionals (eth_call path) tolerates unset optional fields on ANY
/// fork — the legacy builder's `call || fork < Ecotone` rule.
[[nodiscard]] inline eth::EthBlockInfo buildOpEthBlockInfo(
    protocol::BlockHeader const& header, OpForkSpec const& spec, bool lenientOptionals = false)
{
    bool const lenient = lenientOptionals || spec.fork < OpFork::Ecotone;
    auto requireField = [](auto const& opt, char const* name) -> auto const& {
        if (!opt.has_value())
            throw OpEthBlockError(
                std::string("op-eth block: missing required header field ") + name);
        return *opt;
    };
    auto narrowU64 = [](bcos::u256 v, char const* name) {
        if (v > bcos::u256(std::numeric_limits<uint64_t>::max()))
            throw OpEthBlockError(std::string("op-eth block: header ") + name + " overflows u64");
        return static_cast<uint64_t>(v);
    };

    eth::EthBlockInfo blk{};
    blk.number = header.number();
    // FISCO tars store MILLISECONDS; the EVM wants seconds (see toBlockInfo's
    // TIMESTAMP UNIT CONVENTION comment — the /1000 is required and correct).
    blk.timestamp = header.timestamp() / 1000;
    auto const gasLimit = header.gasLimit();
    if (gasLimit > bcos::u256(std::numeric_limits<int64_t>::max()))
        throw OpEthBlockError("op-eth block: header gasLimit overflows int64");
    blk.gas_limit = static_cast<int64_t>(gasLimit);
    blk.base_fee = narrowU64(lenient ? header.baseFee().value_or(bcos::u256{0}) :
                                       requireField(header.baseFee(), "baseFee"),
        "baseFee");
    auto const& cb = header.coinbase();
    if (cb.size() == sizeof(evmc_address))
        std::copy_n(cb.begin(), sizeof(evmc_address), blk.coinbase.bytes);
    auto const randao = header.prevRandao();
    std::copy_n(randao.data(), sizeof(blk.prev_randao.bytes), blk.prev_randao.bytes);
    auto const beaconRoot =
        lenient ? header.parentBeaconBlockRoot().value_or(bcos::h256{}) :
                  requireField(header.parentBeaconBlockRoot(), "parentBeaconBlockRoot");
    std::copy_n(beaconRoot.data(), sizeof(blk.parent_beacon_block_root.bytes),
        blk.parent_beacon_block_root.bytes);
    return blk;
}

/// The complete, post-finalize live MessagePasser slot map (not only this block's
/// modified slots) — the Isthmus+ withdrawalsRoot input. Slot rows are keyed by
/// the raw 32-byte slot (EVMAccount::setStorage); the three core field rows
/// (nonce/balance/codeHash) have short ASCII keys and are skipped by the 32-byte
/// filter. Tombstones and zero values never enter the map (zero slots are
/// deleted at applyToStorage time; the zero filter is defence in depth, matching
/// opStorageRoot's skip).
template <class Storage>
task::Task<std::map<evmc::bytes32, evmc::bytes32>> opEthMessagePasserStorage(Storage& view)
{
    std::map<evmc::bytes32, evmc::bytes32> out;
    auto acc = eth::ethViewAccount(view, OP_L2_TO_L1_MESSAGE_PASSER);
    auto const tableName = std::string(co_await acc.path());
    auto it = co_await storage2::range(
        view, storage2::RANGE_SEEK, executor_v1::StateKey{tableName, std::string_view{}});
    while (auto kv = co_await it.next())
    {
        auto const& [k, v] = *kv;
        executor_v1::StateKeyView keyView(k);
        auto const& [table, key] = keyView.get();
        if (table != tableName)
            break;  // Left this account's table.
        if (key.size() != sizeof(evmc::bytes32))
            continue;  // Core field row (nonce/balance/codeHash), not a slot.
        auto const* entry = std::get_if<storage::Entry>(std::addressof(v));
        if (entry == nullptr)
            continue;  // Tombstone.
        auto const field = entry->get();
        if (field.size() != sizeof(evmc::bytes32))
            throw OpEthBlockError("op-eth block: MessagePasser slot value is not 32 bytes");
        evmc::bytes32 slotKey{};
        evmc::bytes32 slotValue{};
        std::memcpy(slotKey.bytes, key.data(), sizeof(slotKey.bytes));
        std::memcpy(slotValue.bytes, field.data(), sizeof(slotValue.bytes));
        if (evmc::is_zero(slotValue))
            continue;
        out.emplace(slotKey, slotValue);
    }
    co_return out;
}

/// Single-account storage root (secure trie: key = keccak256(slot), value =
/// rlp(trimmed)) — the bcos-evm-free counterpart of OpBlockExecute.cpp's
/// opStorageRoot, sharing bcos::ledger::mpt::computeTrieRoot with it.
[[nodiscard]] inline bcos::h256 opEthStorageRoot(
    const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (const auto& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        size_t first = 0;
        while (first < sizeof(value.bytes) && value.bytes[first] == 0)
            ++first;
        bcos::bytes leaf;
        bcos::codec::rlp::encode(
            leaf, bcos::bytes(value.bytes + first, value.bytes + sizeof(value.bytes)));
        auto const keyHash = eth::evm::keccak256(evmc::bytes_view{key.bytes, sizeof(key.bytes)});
        entries[bcos::h256{keyHash.bytes, sizeof(keyHash.bytes)}] = std::move(leaf);
    }
    return bcos::ledger::mpt::computeTrieRoot(entries).root;
}

/// Header commitments (mirror of sealOpBlock's OpBlockSeal, in framework types).
struct OpEthBlockSeal
{
    bcos::h256 receiptsRoot;
    bcos::Bloom logsBloom{};
    /// Canyon+ (spec.has_withdrawals): Canyon–Holocene the empty-trie root;
    /// Isthmus+ the MessagePasser storage root. Pre-Canyon: nullopt.
    std::optional<bcos::h256> withdrawalsRoot;
    std::optional<bcos::h256> requestsHash;  // Isthmus+ sha256("")
    /// Ecotone+ headers carry blobGasUsed: Jovian+ the DA footprint (Σ of
    /// meta.da_footprint over non-deposit receipts); Ecotone–Isthmus fixed 0.
    /// Pre-Ecotone: nullopt.
    std::optional<uint64_t> blobGasUsed;
};

/// Compute the header commitments (mirror of sealOpBlock).
/// @param messagePasserStorage the complete, post-finalize live MessagePasser
///        slot map (opEthMessagePasserStorage output).
[[nodiscard]] inline OpEthBlockSeal sealOpEthBlock(
    std::vector<protocol::TransactionReceipt::Ptr> const& receipts,
    std::vector<uint8_t> const& txTypes, OpForkSpec const& spec,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage)
{
    if (txTypes.size() != receipts.size())
        throw std::logic_error("op-eth block: receipts/txTypes length mismatch (caller bug)");
    OpEthBlockSeal seal{};

    // receiptsRoot: index-keyed trie over the RLP receipt leaves (op-geth
    // Receipts.EncodeIndex), single-sourced through bcos::ledger::mpt with the
    // legacy seal. The deposit fork-matrix guard is ported from sealOpBlock:
    // the meta fields a deposit receipt carries must match the fork.
    std::vector<bcos::bytes> receiptLeaves;
    receiptLeaves.reserve(receipts.size());
    for (size_t i = 0; i < receipts.size(); ++i)
    {
        if (txTypes[i] == OP_DEPOSIT_TX_TYPE)
        {
            const auto& meta = receipts[i]->opStackMeta();
            const bool hasNonce = meta.has_value() && meta->deposit_nonce.has_value();
            const bool hasVersion = meta.has_value() && meta->deposit_receipt_version.has_value();
            if (spec.has_deposit_receipt_version)  // Canyon+
            {
                if (!hasNonce || !hasVersion)
                    throw OpEthBlockError(
                        "op-eth block: Canyon+ deposit receipt missing deposit "
                        "nonce/receipt version");
            }
            else
            {
                if (hasVersion)
                    throw OpEthBlockError(
                        "op-eth block: pre-Canyon deposit receipt carries deposit "
                        "receipt version");
                if (hasNonce && !spec.regolith_deposit_fixes)
                    throw OpEthBlockError(
                        "op-eth block: pre-Regolith deposit receipt carries deposit nonce");
            }
        }
        try
        {
            receiptLeaves.push_back(ledger::mpt::encodeReceiptLeaf(*receipts[i], txTypes[i]));
        }
        catch (ledger::mpt::EthReceiptEncodeError const& e)
        {
            throw OpEthBlockError(std::string("op-eth block: ") + e.what());
        }
    }
    std::vector<bcos::bytesConstRef> receiptLeafRefs;
    receiptLeafRefs.reserve(receiptLeaves.size());
    for (auto const& leaf : receiptLeaves)
        receiptLeafRefs.emplace_back(leaf.data(), leaf.size());
    seal.receiptsRoot = ledger::mpt::calculateReceiptsRoot(receiptLeafRefs);

    // Block-level logsBloom = bitwise-OR of each receipt's 256-byte bloom.
    for (const auto& r : receipts)
    {
        auto const bloom = r->logsBloom();
        if (bloom.size() != seal.logsBloom.size())
            throw OpEthBlockError("op-eth block: receipt logsBloom must be 256 bytes, got " +
                                  std::to_string(bloom.size()));
        for (size_t i = 0; i < seal.logsBloom.size(); ++i)
            seal.logsBloom[i] |= bloom[i];
    }

    // withdrawalsRoot by fork (op-geth: the header field exists from Canyon on;
    // the L2 never processes withdrawal credits — OP headers carry an
    // always-empty withdrawals list):
    //   pre-Canyon      -> nullopt (field absent)
    //   Canyon–Holocene -> empty-trie root
    //   Isthmus+        -> L2ToL1MessagePasser storage root
    // requestsHash: Isthmus+ sha256(""); pre-Isthmus nullopt.
    if (spec.fork >= OpFork::Isthmus)
    {
        seal.withdrawalsRoot = opEthStorageRoot(messagePasserStorage);
        // sha256(""), single-sourced from the framework constant.
        auto const raw = bcos::fromHex(std::string{bcos::engine::c_emptyRequestsHashHex});
        if (raw.size() != sizeof(bcos::h256))
            throw std::logic_error("c_emptyRequestsHashHex must decode to exactly 32 bytes");
        bcos::h256 requestsHash{};
        std::copy(raw.begin(), raw.end(), requestsHash.begin());
        seal.requestsHash = requestsHash;
    }
    else if (spec.has_withdrawals)  // Canyon–Holocene
    {
        seal.withdrawalsRoot = ledger::mpt::emptyRootHash();
    }

    // blobGasUsed by fork: Jovian+ the DA footprint (Σ da_footprint over
    // non-deposit receipts; deposits carry nullopt and are skipped; a missing
    // optional on a non-deposit receipt is a consensus reject, not a silent 0 —
    // a deposits-only block sums no terms and is always 0 ≡ op-geth's
    // first-Jovian-block special case). Ecotone–Isthmus: fixed 0.
    if (spec.has_da_footprint)
    {
        uint64_t footprint = 0;
        for (size_t i = 0; i < receipts.size(); ++i)
        {
            if (txTypes[i] == OP_DEPOSIT_TX_TYPE)
                continue;
            const auto& meta = receipts[i]->opStackMeta();
            if (!meta || !meta->da_footprint)
                throw OpEthBlockError(
                    "op-eth block: non-deposit receipt missing da_footprint under Jovian");
            auto const term = *meta->da_footprint;
            if (footprint > std::numeric_limits<uint64_t>::max() - term)
                throw OpEthBlockError("op-eth block: DA footprint overflows uint64");
            footprint += term;
        }
        seal.blobGasUsed = footprint;
    }
    else if (spec.fork >= OpFork::Ecotone)
    {
        seal.blobGasUsed = uint64_t{0};
    }
    return seal;
}
}  // namespace bcos::executor_v1::opstack
