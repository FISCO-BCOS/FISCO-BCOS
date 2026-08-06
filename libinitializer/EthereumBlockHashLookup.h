/// @file EthereumBlockHashLookup.h
/// @brief The storage-backed BLOCKHASH provider for the EthereumExecutor
///        (executor_version=2). Extracted as a small header so the production
///        wiring (libinitializer) and the scheduler integration test share the
///        exact same provider instead of carrying parallel copies.
#pragma once

#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/TBBWait.h"
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <algorithm>
#include <cstddef>

namespace bcos::initializer
{
/// Resolve a committed block hash for the EthereumExecutor (executor_version=2)
/// BLOCKHASH opcode. Reads straight from the committed SYS_NUMBER_2_HASH table
/// via the ledger::getBlockHash LedgerMethod — one storage read, no cache.
///
/// @param currentHeight the height of the block being executed, passed in from
///        the executor's execution context (EthereumHost knows it as
///        m_block.number), so no storage read for the current height is needed
///        here. It bounds BLOCKHASH to the last 256 ancestors (Yellow Paper
///        H.4): the visible ancestors are [currentHeight - 256, currentHeight - 1].
///        Fails closed: a broken backend or an out-of-window request reports an
///        unknown block (zero hash).
template <class Backend>
evmc::bytes32 ethBlockHashLookupFromStorage(
    Backend& backend, int64_t blockNumber, int64_t currentHeight)
{
    constexpr int64_t kMaxBlockHashLookback = 256;
    if (blockNumber < 0)
    {
        // ledger::getBlockHash rejects negative heights; a BLOCKHASH of a
        // negative number is "unknown" anyway.
        return {};
    }
    // Bound BLOCKHASH to the last 256 ancestors (Yellow Paper H.4). currentHeight
    // is the height of the block being executed, so the visible ancestors are
    // [currentHeight - 256, currentHeight - 1]: the executing block itself is
    // never an ancestor, and the oldest reachable one is currentHeight - 256
    // (matches geth's upper-256 <= n < upper).
    if (currentHeight < 0 || blockNumber >= currentHeight ||
        currentHeight - blockNumber > kMaxBlockHashLookback)
    {
        // Not among the last 256 ancestors — unknown block.
        return {};
    }

    try
    {
        auto hashOpt = task::tbb::syncWait(
            bcos::ledger::getBlockHash(backend, blockNumber, bcos::ledger::fromStorage));
        evmc::bytes32 result{};
        if (hashOpt.has_value())
        {
            auto const& hash = *hashOpt;
            std::copy_n(hash.data(), std::min<size_t>(hash.size(), sizeof(evmc_bytes32)),
                result.bytes);
        }
        return result;
    }
    catch (...)
    {
        // Never let a lookup failure cross the noexcept boundary.
        return {};
    }
}
}  // namespace bcos::initializer
