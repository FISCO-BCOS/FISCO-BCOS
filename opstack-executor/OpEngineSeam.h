// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpEngineSeam.h — the engine-facing publication surface of the OP seam.
//
// `engine/bcos-engine` must not gain a dependency on `bcos-evm` (library purity: its CMake target
// links only bcos-framework/bcos-task/bcos-utilities/ledger), so the newPayload OP branch reaches
// every OP-specific thing it needs (block-env struct, error taxonomy, tx-root derivation, header
// table name, comparison surface) as a **dependent name on the SchedulerType template parameter**,
// re-published by `OpSchedulerImpl`. This header holds the actual definitions (in bcos:: types);
// it deliberately does NOT include `OpSchedulerImpl.h` (that would be circular).

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpRlpDecode.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm::engine
{

/// (REMOVED 2026-08-05) `SYS_ETH_BLOCK_HEADER`/"s_eth_block_header" was retired: OP headers now
/// land in the standard `bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER` ("s_number_2_header") as tars
/// `protocol::BlockHeader`.

/// Table holding each accepted OP block's transactions as their **raw EIP-2718 envelopes**, keyed
/// by `keccak(envelope)` — the Ethereum transaction hash, the same key `SYS_HASH_2_RECEIPT` uses,
/// so a transaction and its receipt are retrievable under one key. OP-only table; its name lives
/// here, not in bcos-framework's LedgerTypeDef.h.
///
/// Deliberately NOT the generic `SYS_HASH_2_TX`: that table holds tars-encoded
/// `bcos::protocol::Transaction` objects whose readers hand bytes straight to the transaction
/// factory — an Ethereum envelope would decode into an all-default, plausible-looking transaction
/// whose hash does not match the key it was stored under, reaching `eth_getTransactionByHash`
/// responses and txpool consensus-proposal verification. Mapping to a real `protocol::Transaction`
/// is also unavailable (type 0x04/0x7E rejected, no tars slots for sourceHash/mint/
/// authorizationList, and `Transaction::verify` would ecrecover a signature-less deposit into a
/// fabricated sender). Storing the faithful bytes under an OP-specific name keeps every option
/// open.
inline constexpr std::string_view SYS_ETH_HASH_2_RAWTX{"s_eth_hash_2_rawtx"};

namespace detail
{
/// evmc::bytes32 -> bcos::h256 (the `FixedBytes(byte const*, size_t)` constructor, same
/// conversion `Storage2State.h`'s `applyModifiedEntry` uses for codeHash). Defined here rather
/// than in `OpSchedulerImpl.h` (which includes this header and uses this definition) so there is
/// exactly one definition of this name in `bcos::evm::engine::detail`.
inline bcos::h256 toBcosH256(const evmc::bytes32& hash)
{
    return bcos::h256(reinterpret_cast<const bcos::byte*>(hash.bytes), sizeof(hash.bytes));
}

/// evmone::state::BloomFilter (256 raw bytes) -> bcos::h2048, the type
/// `bcos::codec::rlp::EthBlockHeader::logsBloom` is declared with (Task 3).
inline bcos::h2048 toBcosBloom(const evmone::state::BloomFilter& bloom)
{
    return bcos::h2048(reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes));
}

/// Builds the L1-attributes deposit envelope the sequencer must inject as the first transaction of
/// every OP block (op-geth: the EL builds the first deposit tx; processOpBlock requires it — an
/// empty/non-deposit-first block is rejected). Format: `0x7E || rlp([sourceHash(32B), from(20B),
/// to(20B), mint, value, gas, isSystemTx, data])` — the canonical deposit envelope `decodeDepositTx`
/// reads back. calldata is the Isthmus L1-attributes shape: `0x098999be` (setL1BlockValues
/// selector) + 12 fields × 14 bytes (0-padded big-endian uint64/uint128, L1Block.sol encoding)
/// + 4 trailing zero bytes = 176 bytes (IsthmusL1AttributesLen). sourceHash is derived as
/// keccak256("l1AttributesDeposit" || blockNumber || sequenceNumber) — a deterministic unique id
/// (op-geth derives it from the L1 tx; a local sequencer has no L1, so a local derivation is the
/// faithful analogue). The exact calldata values are not consensus-critical for the shape check —
/// L1Block.sol stores them as state the L2 can read.
inline bcos::bytes makeL1AttributesDeposit(uint64_t blockNumber, uint64_t sequenceNumber,
    uint64_t timestamp, uint64_t baseFee, uint64_t gasLimit, uint64_t chainId, bool isJovian)
{
    using bcos::codec::rlp::encode;
    constexpr std::uint8_t kDepositTypeByte = 0x7e;
    (void)baseFee;  // L2 base fee — not part of the L1-attributes calldata

    // L1-attributes calldata in op-geth's L1Block.sol setL1BlockValues encoding — the layout the
    // executor's C-3/C-4 shape validation (OpBlockExecute.cpp validateJovianBlockShape) and the
    // L1Block contract expect. (An earlier 12×14B analogue satisfied only the length/selector
    // check and wrote garbage L1Block slots; it broke as soon as a user tx joined the block —
    // 176B reads as a Jovian activation block, which must be deposits-only.)
    //   Isthmus: 176B, selector 0x098999be, [4:8] baseFeeScalar, [8:12] blobBaseFeeScalar,
    //            [12:20] sequenceNumber, [20:28] l1Timestamp, [28:36] l1Number,
    //            [36:68] l1BaseFee, [68:100] blobBaseFee, [100:132] l1Hash, [132:164] batcherHash,
    //            [164:168] opFeeScalar, [168:176] opFeeConstant.
    //   Jovian:  178B, selector 0x3db6be2b, + [176:178] daFootprintGasScalar.
    // L1-specific values are zero — a local sequencer has no L1 — so l1/operator/DA fees are 0
    // and the block's only gas cost is the L2 base fee.
    constexpr std::array<uint8_t, 4> kIsthmusSel = {0x09, 0x89, 0x99, 0xbe};
    constexpr std::array<uint8_t, 4> kJovianSel = {0x3d, 0xb6, 0xbe, 0x2b};
    const size_t calldataLen = isJovian ? 178 : 176;
    bcos::bytes data(calldataLen, 0);
    auto const& sel = isJovian ? kJovianSel : kIsthmusSel;
    std::copy(sel.begin(), sel.end(), data.begin());
    auto putBe = [&data](size_t offset, uint64_t value, size_t width) {
        for (size_t i = 0; i < width; ++i)
            data[offset + width - 1 - i] = static_cast<bcos::byte>((value >> (i * 8)) & 0xFF);
    };
    putBe(4, 0, 4);                 // baseFeeScalar = 0
    putBe(8, 0, 4);                 // blobBaseFeeScalar = 0
    putBe(12, sequenceNumber, 8);   // sequenceNumber
    putBe(20, timestamp, 8);        // L1 timestamp (L2 block timestamp analogue)
    putBe(28, blockNumber, 8);      // L1 number (L2 block number analogue)
    // [36:132] l1BaseFee / blobBaseFee / l1Hash / batcherHash stay zero.
    // [132:164] batcherHash zero, [164:168] opFeeScalar zero, [168:176] opFeeConstant zero.
    if (isJovian)
        putBe(176, 0, 2);           // daFootprintGasScalar = 0

    // sourceHash: keccak256("l1AttributesDeposit" || blockNumber || sequenceNumber) — deterministic
    // unique per block. bcos::crypto::keccak256Hash is available transitively; a 32-byte zero hash
    // would also pass decodeDepositTx (it only length-checks), but a real derivation is safer for
    // downstream uniqueness assumptions.
    bcos::bytes sourceInput = bcos::bytes{'l', '1', 'A', 't', 't', 'r', 'i', 'b', 'u', 't', 'e',
        's', 'D', 'e', 'p', 'o', 's', 'i', 't'};
    for (int i = 7; i >= 0; --i)
        sourceInput.push_back(static_cast<bcos::byte>((blockNumber >> (i * 8)) & 0xFF));
    for (int i = 7; i >= 0; --i)
        sourceInput.push_back(static_cast<bcos::byte>((sequenceNumber >> (i * 8)) & 0xFF));
    auto sourceHash = bcos::crypto::keccak256Hash(bcos::ref(sourceInput));
    const auto sourceHashBytes = sourceHash.asBytes();

    // Deposit envelope fields.
    const bcos::bytes kFrom{0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0xde,
        0xad, 0xde, 0xad, 0xde, 0xad, 0xde, 0xad, 0x00, 0x01};  // OP_DEPOSITOR 0xdead..0001
    const bcos::bytes kTo{0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15};  // L1Block 0x4200..15
    constexpr uint64_t kMint = 0, kValue = 0, kIsSystemTx = 0;
    constexpr uint64_t kDepositGas = 1'000'000;  // well above intrinsic; L1Block call needs little

    // The variadic `encode(to, args...)` already wraps the fields in an RLP list header
    // (RLPEncode.h:141-147) — the deposit envelope is `0x7E || rlp_list([...])`, so the list
    // payload goes straight after the type byte, no second list header.
    bcos::bytes envelope;
    envelope.push_back(kDepositTypeByte);
    encode(envelope, sourceHashBytes, kFrom, kTo, kMint, kValue, kDepositGas, kIsSystemTx, data);
    return envelope;
}
}  // namespace detail

/// The block-execution commitments the engine's newPayload OP branch compares against the
/// payload, restated in bcos:: types.
///
/// Two roles in one struct:
///   - the **six-way comparison surface** proper (exactly six, no more):
///     `receiptsRoot`/`logsBloom`/`withdrawalsRoot` (from `OpBlockSeal`) +
///     `stateRoot`/`gasUsed`/`txRoot` (from `OpExecuteBlockResult`'s own three members);
///   - two further seal outputs assigned to seal comparison and therefore NOT counted among the
///     six: `blobGasUsed` (engaged from Jovian on, where the header slot is repurposed as the DA
///     footprint — carried by seal comparison) and `requestsHash` (engaged Isthmus+; the engine
///     reconstructs the header with its own `OP_EMPTY_REQUESTS_HASH` copy of this protocol
///     constant and comparing catches any drift between the two copies).
///
/// Type choices mirror what the engine holds on the other side of each comparison:
/// `ExecutionPayload::gasUsed` is `u256`, `logsBloom` becomes `h2048` (the `EthBlockHeader` field
/// type), everything else is `h256`.
struct OpBlockCommitments
{
    bcos::h256 receiptsRoot;
    bcos::h2048 logsBloom;
    bcos::h256 withdrawalsRoot;
    bcos::h256 stateRoot;
    bcos::u256 gasUsed;
    bcos::h256 txRoot;
    std::optional<uint64_t> blobGasUsed;
    std::optional<bcos::h256> requestsHash;
};

/// Projects an executed block's seal + the three standalone result members into
/// `OpBlockCommitments`. Takes the pieces (not `OpExecuteBlockResult`) so this header stays
/// independent of `OpSchedulerImpl.h`; that class supplies the field projection.
inline OpBlockCommitments commitmentsOf(const bcos::evm::opstack::OpBlockSeal& seal,
    const bcos::h256& stateRoot, uint64_t gasUsed, const bcos::h256& txRoot)
{
    OpBlockCommitments out{
        .receiptsRoot = detail::toBcosH256(seal.receiptsRoot),
        .logsBloom = detail::toBcosBloom(seal.logsBloom),
        .withdrawalsRoot = detail::toBcosH256(seal.withdrawalsRoot),
        .stateRoot = stateRoot,
        .gasUsed = bcos::u256(gasUsed),
        .txRoot = txRoot,
        .blobGasUsed = seal.blobGasUsed,
        .requestsHash = std::nullopt,
    };
    if (seal.requestsHash.has_value())
    {
        out.requestsHash = detail::toBcosH256(*seal.requestsHash);
    }
    return out;
}

/// Payload Bloom (std::array<byte,256>) → bcos::h2048, byte-faithful (mirrors the engine's
/// detail::toEthLogsBloom, moved OP-side with the projection). Named distinctly from the
/// existing detail::toBcosBloom (evmone::state::BloomFilter overload) to avoid confusion.
inline bcos::h2048 payloadBloomToH2048(const std::array<bcos::byte, 256>& bloom)
{
    bcos::h2048 out;
    std::memcpy(out.data(), bloom.data(), bloom.size());
    return out;
}

/// Projects the payload/header announced commitments into OpBlockCommitments (the "announced"
/// side of mismatchedFieldOf). 5 fields from ExecutionPayload, txRoot from the caller's
/// computeTxRoot, blobGasUsed reverse-narrowed (payload optional<u256> → optional<uint64_t>;
/// narrow is total because validateOpNewPayloadRequest already bounds it ≤ UINT64_MAX,
/// EngineServiceImpl.cpp:475-477), requestsHash from the rebuilt header. withdrawalsRoot /
/// blobGasUsed deref is safe: the engine validation guarantees them present (design doc §4).
inline OpBlockCommitments announcedCommitmentsOf(
    const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
    const bcos::protocol::BlockHeader& ethHeader)
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

/// Compares the executed block's commitments against the payload's announced commitments.
/// Returns the mismatching field name (first in comparison order), or nullopt if all match.
/// Contract (verbatim port of the engine's comparison block, zero judgment change):
///   - fields compared in order receiptsRoot → logsBloom → withdrawalsRoot → stateRoot →
///     gasUsed → txRoot → blobGasUsed → requestsHash; first mismatch wins;
///   - the txRoot slot reports the literal "transactionsRoot" (not "txRoot");
///   - blobGasUsed / requestsHash are compared only when the COMPUTED side has a value, and the
///     announced side is dereferenced (guaranteed present by the engine validation — see design
///     doc §4); computed-side nullopt skips regardless of announced.
inline std::optional<std::string> mismatchedFieldOf(
    const OpBlockCommitments& computed, const OpBlockCommitments& announced)
{
    if (computed.receiptsRoot != announced.receiptsRoot)
        return "receiptsRoot";
    if (computed.logsBloom != announced.logsBloom)
        return "logsBloom";
    if (computed.withdrawalsRoot != announced.withdrawalsRoot)
        return "withdrawalsRoot";
    if (computed.stateRoot != announced.stateRoot)
        return "stateRoot";
    if (computed.gasUsed != announced.gasUsed)
        return "gasUsed";
    if (computed.txRoot != announced.txRoot)
        return "transactionsRoot";
    if (computed.blobGasUsed.has_value() && *computed.blobGasUsed != *announced.blobGasUsed)
        return "blobGasUsed";
    if (computed.requestsHash.has_value() && *computed.requestsHash != announced.requestsHash.value())
        return "requestsHash";
    return std::nullopt;
}

/// transactionsRoot over the block's raw EIP-2718 envelopes: trie key = canonical RLP of the
/// index, trie value = **the raw wire bytes as received**.
///
/// NOTE: this is NOT op-geth's `DeriveSha` convention — `types.DeriveSha` RE-ENCODES each
/// transaction canonically from the parsed struct, while this hashes the bytes as they arrived.
/// The two agree for canonical input and disagree for non-canonical input (two different block
/// hashes for one payload); they coincide only because `OpSchedulerImpl`'s raw-tx decoders reject
/// every non-canonical encoding Go's `rlp` rejects — per-field strictness, the shared length-prefix
/// fix, and the `assertCanonicalRoundTrip` whole-envelope backstop. If that decoder strictness is
/// ever relaxed, this equivalence lapses with it, which is why the round-trip invariant exists:
/// to fail closed on a future change.
///
/// Factored out of `OpSchedulerImpl::executeOpBlock`'s step 6 because the engine needs the *same*
/// value **before** execution: `ExecutionPayload` carries no `transactionsRoot` field, so the
/// header reconstruction the blockHash check depends on (a static check that must precede
/// parentKnown/execution) has to derive it — exactly as op-geth's `ExecutableDataToBlock` does
/// before comparing block hashes. One function, two call sites: the six-way surface's `txRoot`
/// comparison then verifies that execution's own derivation did not diverge from this one.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    // txRoot trie: key = rlp(index) (ascending, non-prefix-of-each-other), leaf = raw envelope
    // bytes as-is. Built with FISCO computeTrieRootVarKey (same construction as the retired
    // evmone list-trie mpt_hash.cpp:38-46).
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
    auto result = bcos::ledger::mpt::computeTrieRootVarKey(entries);
    return result.root;  // already bcos::h256 (computeTrieRootVarKey returns bcos::h256)
}

}  // namespace bcos::evm::engine
