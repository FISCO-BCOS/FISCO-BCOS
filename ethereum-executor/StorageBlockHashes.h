/// @file StorageBlockHashes.h
/// @brief An evmone::state::BlockHashes implementation backed by BCOS storage.
///
/// Resolves block hashes through the ledger::getBlockHash LedgerMethod
/// (storage-based tag_invoke in bcos-ledger/LedgerMethods.h, which reads the
/// SYS_NUMBER_2_HASH table). This is the "read blockHashes from Storage based
/// on LedgerMethod" provider that can be injected into EthereumExecutor's
/// constructor.
///
/// This header deliberately lives apart from EthereumExecutor.h: it pulls in
/// the bcos-ledger LedgerMethods header (and transitively the executor / tars
/// protocol headers), so only the TUs that actually need a storage-backed
/// block-hash provider include it. EthereumExecutor itself only depends on the
/// evmone::state::BlockHashes interface.

#pragma once

#include "bcos-evm/eth/state/state_view.hpp"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/BoostLog.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace bcos::executor_v1::eth
{

/// A BlockHashes provider backed by BCOS storage, safe for concurrent lookups.
///
/// evmone::state::BlockHashes::get_block_hash is a synchronous noexcept
/// virtual, so the async storage read is bridged with task::tbb::syncWait.
/// Lookups are resolved through the ledger::getBlockHash LedgerMethod (a
/// read-only read of the committed SYS_NUMBER_2_HASH table). Recently-resolved
/// hashes are memoized behind a mutex, so concurrent lookups — the pipeline may
/// run prepare/execute/finish of different transactions in parallel — are
/// served from the cache; a cache miss still performs the storage read, and
/// that read relies on the injected Storage's support for concurrent reads.
/// Block hashes are immutable once committed, so caching them is safe.
///
/// Unknown block numbers (including heights the chain has not reached, and any
/// negative input) read as a zero hash — Ethereum semantics for BLOCKHASH of an
/// unknown ancestor. A storage backend failure is logged and likewise reported
/// as a zero hash, so an exception can never cross the noexcept boundary.
template <class Storage>
class StorageBlockHashes : public evmone::state::BlockHashes
{
public:
    explicit StorageBlockHashes(Storage& storage) : m_storage(std::ref(storage)) {}

    evmc::bytes32 get_block_hash(int64_t blockNumber) const noexcept override
    {
        if (blockNumber < 0)
        {
            // ledger::getBlockHash rejects negative heights by throwing; a
            // BLOCKHASH of a negative number is "unknown" anyway.
            return {};
        }

        {
            std::lock_guard lock(m_mutex);
            if (auto it = m_cache.find(blockNumber); it != m_cache.end())
                return it->second;
        }

        evmc::bytes32 result{};
        try
        {
            auto hashOpt = task::tbb::syncWait(
                ledger::getBlockHash(m_storage.get(), blockNumber, ledger::fromStorage));
            if (hashOpt.has_value())
            {
                auto const& hash = *hashOpt;
                std::copy_n(hash.data(),
                    std::min<size_t>(hash.size(), sizeof(evmc_bytes32)), result.bytes);
            }
        }
        catch (std::exception const& e)
        {
            // A failed storage read is reported as an unknown block (zero
            // hash) — never let an exception cross the noexcept boundary.
            BCOS_LOG(ERROR) << LOG_DESC("StorageBlockHashes: storage read failed")
                            << LOG_KV("blockNumber", blockNumber) << LOG_KV("reason", e.what());
            return {};
        }
        catch (...)
        {
            BCOS_LOG(ERROR) << LOG_DESC("StorageBlockHashes: storage read failed")
                            << LOG_KV("blockNumber", blockNumber);
            return {};
        }

        // Only memoize hashes that were actually found; an unknown height must
        // not be pinned to zero forever (it may become known later).
        if (result != evmc::bytes32{})
        {
            std::lock_guard lock(m_mutex);
            if (m_cache.size() >= kMaxCacheSize)
                m_cache.clear();  // simple bounded cache; eviction is just a perf reset
            m_cache.emplace(blockNumber, result);
        }
        return result;
    }

private:
    static constexpr size_t kMaxCacheSize = 256;
    std::reference_wrapper<Storage> m_storage;
    mutable std::mutex m_mutex;
    mutable std::unordered_map<int64_t, evmc::bytes32> m_cache;
};

}  // namespace bcos::executor_v1::eth
