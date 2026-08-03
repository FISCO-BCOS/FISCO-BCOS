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
#include <evmc/evmc.h>
#include <algorithm>
#include <functional>

namespace bcos::executor_v1::eth
{

/// A thread-safe BlockHashes provider backed by BCOS storage.
///
/// evmone::state::BlockHashes::get_block_hash is a synchronous noexcept
/// virtual, so the async storage read is bridged with task::tbb::syncWait.
/// Every lookup reads straight from the storage (via the ledger::getBlockHash
/// LedgerMethod). The reads are read-only (committed block hashes), so
/// concurrent lookups — the pipeline may run prepare/execute/finish of
/// different transactions in parallel — rely on the storage's support for
/// concurrent reads. Unknown block numbers read as a zero hash (Ethereum
/// semantics for BLOCKHASH of an unknown ancestor).
template <class Storage>
class StorageBlockHashes : public evmone::state::BlockHashes
{
public:
    explicit StorageBlockHashes(Storage& storage) : m_storage(std::ref(storage)) {}

    evmc::bytes32 get_block_hash(int64_t blockNumber) const noexcept override
    {
        try
        {
            auto hashOpt = task::tbb::syncWait(
                ledger::getBlockHash(m_storage.get(), blockNumber, ledger::fromStorage));

            evmc::bytes32 result{};
            if (hashOpt.has_value())
            {
                auto const& hash = *hashOpt;
                std::copy_n(
                    hash.data(), std::min<size_t>(hash.size(), sizeof(evmc_bytes32)), result.bytes);
            }
            return result;
        }
        catch (...)
        {
            // A failed storage read is reported as an unknown block (zero
            // hash) — never let an exception cross the noexcept boundary.
            return {};
        }
    }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::executor_v1::eth
