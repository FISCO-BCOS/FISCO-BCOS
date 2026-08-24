// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// The six-way commitment comparison surface: the executed block's commitments vs the payload's
// announced commitments (restated in bcos:: types), plus the seal-only outputs (blobGasUsed /
// requestsHash). Split out of OpCommon.h so that header stays a pure types header (errors +
// result structs) and this functional surface lives with the conversions it uses.

#include <opstack-executor/OpCommon.h>  // OpBlockSeal / OpExecuteBlockResult

#include <bcos-framework/ledger/Account.h>  // bcos::ledger::account::toH256
#include <bcos-utilities/FixedBytes.h>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <optional>
#include <string>

namespace bcos::evm::engine
{
namespace detail
{
/// evmc::bytes32 -> bcos::h256. Delegates to the ledger-account home of the same conversion
/// (bcos::ledger::account::toH256) instead of keeping a byte-level duplicate.
inline bcos::h256 toBcosH256(const evmc::bytes32& hash)
{
    return bcos::ledger::account::toH256(hash);
}

/// evmone::state::BloomFilter (256 raw bytes) -> bcos::h2048.
inline bcos::h2048 toBcosBloom(const evmone::state::BloomFilter& bloom)
{
    return bcos::h2048(reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes));
}
}  // namespace detail

/// The block-execution commitments the engine's newPayload OP branch compares against the
/// payload, restated in bcos:: types: the six-way comparison surface (receiptsRoot/logsBloom/
/// withdrawalsRoot from OpBlockSeal + stateRoot/gasUsed/txRoot) plus two seal-only outputs
/// (blobGasUsed, engaged from Jovian on; requestsHash, engaged Isthmus+).
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

/// Project an executed block's seal + the three standalone result members into OpBlockCommitments.
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

/// Payload bloom (std::array<byte,256>) → bcos::h2048, byte-faithful.
inline bcos::h2048 payloadBloomToH2048(const std::array<bcos::byte, 256>& bloom)
{
    bcos::h2048 out;
    std::memcpy(out.data(), bloom.data(), bloom.size());
    return out;
}

/// Compare the executed block's commitments against the payload's announced commitments; returns
/// the first mismatching field name (txRoot slot reports "transactionsRoot"), or nullopt.
/// blobGasUsed / requestsHash compare presence AND value bidirectionally (optional != optional):
/// an announced-only field (peer ahead of the local fork config) is rejected just as a
/// computed-only one is — op-geth's engine API rejects fork-field asymmetry in both directions.
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
    // Presence asymmetry is a real mismatch (fork-config divergence between the peers), reported
    // rather than crashing with bad_optional_access or silently passing.
    if (computed.blobGasUsed != announced.blobGasUsed)
        return "blobGasUsed";
    if (computed.requestsHash != announced.requestsHash)
        return "requestsHash";
    return std::nullopt;
}
}  // namespace bcos::evm::engine
