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

#include <test/state/state_view.hpp>
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/BoostLog.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>

namespace bcos::executor_v1::eth
{

/// A BlockHashes provider backed by BCOS storage, safe for concurrent lookups.
///
/// evmone::state::BlockHashes::get_block_hash is a synchronous noexcept
/// virtual, so the async storage read is bridged with task::tbb::syncWait.
/// Every lookup reads the committed SYS_NUMBER_2_HASH table straight through
/// the injected Storage (via the ledger::getBlockHash LedgerMethod) — there is
/// no local cache. The reads are read-only (committed block hashes), so
/// concurrent lookups — the pipeline may run prepare/execute/finish of
/// different transactions in parallel — rely on the injected Storage's support
/// for concurrent reads.
///
/// Only the last 256 ancestor block hashes are reachable through BLOCKHASH
/// (Ethereum yellow paper H.4). The current committed height is read from
/// storage via ledger::getCurrentBlockNumber; a request outside
/// [currentHeight - 255, currentHeight] is reported as an unknown block (zero
/// hash) without touching the hash table. The bound is waived only when the
/// chain has no committed height yet (a height of -1, e.g. genesis or
/// standalone execution). A *failed* height read fails closed — reported as an
/// unknown block — so a broken backend can never make an arbitrarily old hash
/// resolvable by skipping the bound. Any negative input, an out-of-window
/// request, or a storage backend failure is reported as zero; an exception can
/// never cross the noexcept boundary.
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

        // Bound BLOCKHASH to the last 256 ancestors. currentHeight is the last
        // committed height (the parent of the block being executed), so the
        // reachable range is [currentHeight - 255, currentHeight].
        //
        // The two "height unknown" cases are deliberately handled differently:
        // a chain with no committed height yet (genesis/standalone) reports -1
        // and waives the bound, while a *failed* height read fails closed (an
        // unknown block) — a broken backend must never make an arbitrarily old
        // hash resolvable by skipping the bound.
        std::optional<int64_t> currentHeight;
        try
        {
            currentHeight = task::tbb::syncWait(
                ledger::getCurrentBlockNumber(m_storage.get(), ledger::fromStorage));
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(ERROR) << LOG_DESC("StorageBlockHashes: getCurrentBlockNumber failed")
                            << LOG_KV("blockNumber", blockNumber) << LOG_KV("reason", e.what());
            return {};
        }
        catch (...)
        {
            BCOS_LOG(ERROR) << LOG_DESC("StorageBlockHashes: getCurrentBlockNumber failed")
                            << LOG_KV("blockNumber", blockNumber);
            return {};
        }

        if (currentHeight.has_value() && *currentHeight >= 0 &&
            (blockNumber > *currentHeight ||
                *currentHeight - blockNumber > kMaxBlockHashLookback - 1))
        {
            // Not among the last kMaxBlockHashLookback ancestors — unknown block.
            return {};
        }

        try
        {
            auto hashOpt = task::tbb::syncWait(
                ledger::getBlockHash(m_storage.get(), blockNumber, ledger::fromStorage));

            evmc::bytes32 result{};
            if (hashOpt.has_value())
            {
                auto const& hash = *hashOpt;
                std::copy_n(hash.data(),
                    std::min<size_t>(hash.size(), sizeof(evmc_bytes32)), result.bytes);
            }
            return result;
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
    }

private:
    // Maximum number of ancestor block hashes reachable via BLOCKHASH.
    static constexpr int64_t kMaxBlockHashLookback = 256;
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::executor_v1::eth
