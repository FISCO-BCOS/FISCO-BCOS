/// @file OpEthCommitments.cpp
/// @brief Non-template parts of OpEthCommitments.h: the computed/announced
///        projections and the six-way comparison. Ported from OpCommitments.h
///        (commitmentsOf/mismatchedFieldOf) and OpBlockExecute.h
///        (announcedCommitmentsOf) with the seal fields already in framework
///        types — the evmone BloomFilter/hash256 conversions are gone.

#include <opstack-executor/OpEthCommitments.h>

#include <opstack-executor/OpCommon.h>  // bcos::evm::OpConsensusError (unchanged home)
#include <cstring>

namespace bcos::executor_v1::opstack
{
namespace
{
/// bcos::Bloom (std::array<byte,256>) → bcos::h2048, byte-faithful. Covers
/// both producers (the seal's block-level bloom and the payload's announced
/// bloom — both are bcos::Bloom on this layer).
[[nodiscard]] bcos::h2048 bloomToH2048(const bcos::Bloom& bloom)
{
    bcos::h2048 out;
    std::memcpy(out.data(), bloom.data(), bloom.size());
    return out;
}

using bcos::evm::engine::detail::narrowU256ToU64;

}  // namespace

OpEthBlockCommitments opEthCommitmentsOf(
    const OpEthBlockSeal& seal, const bcos::h256& stateRoot, uint64_t gasUsed,
    const bcos::h256& txRoot)
{
    return OpEthBlockCommitments{
        .receiptsRoot = seal.receiptsRoot,
        .logsBloom = bloomToH2048(seal.logsBloom),
        .withdrawalsRoot = seal.withdrawalsRoot,
        .stateRoot = stateRoot,
        .gasUsed = bcos::u256(gasUsed),
        .txRoot = txRoot,
        .blobGasUsed = seal.blobGasUsed,
        .requestsHash = seal.requestsHash,
    };
}

OpEthBlockCommitments announcedOpEthCommitmentsOf(const bcos::engine::ExecutionPayload& payload,
    const bcos::h256& transactionsRoot, const bcos::protocol::BlockHeader& ethHeader)
{
    // Isthmus+ payloads always carry withdrawalsRoot (upstream invariant), but if that ever
    // lapses the unconditional deref below would throw bad_optional_access (-> UnknownError).
    // Guard it into a clean consensus-level rejection naming the field (symmetric to
    // opEthMismatchedFieldOf's report).
    if (!payload.withdrawalsRoot.has_value())
        throw bcos::evm::OpConsensusError("op block: payload missing withdrawalsRoot");
    return OpEthBlockCommitments{
        .receiptsRoot = payload.receiptsRoot,
        .logsBloom = bloomToH2048(payload.logsBloom),
        .withdrawalsRoot = *payload.withdrawalsRoot,
        .stateRoot = payload.stateRoot,
        .gasUsed = payload.gasUsed,
        .txRoot = transactionsRoot,
        .blobGasUsed = payload.blobGasUsed.has_value() ?
                           std::optional<uint64_t>(narrowU256ToU64(
                               *payload.blobGasUsed, "ExecutionPayload.blobGasUsed")) :
                           std::nullopt,
        .requestsHash = ethHeader.requestsHash(),
    };
}

std::optional<std::string> opEthMismatchedFieldOf(
    const OpEthBlockCommitments& computed, const OpEthBlockCommitments& announced)
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
    // Presence asymmetry is a real mismatch (fork-config divergence between the peers), reported
    // rather than crashing with bad_optional_access or silently passing.
    if (computed.blobGasUsed != announced.blobGasUsed)
        return "blobGasUsed";
    if (computed.requestsHash != announced.requestsHash)
        return "requestsHash";
    return std::nullopt;
}
}  // namespace bcos::executor_v1::opstack
