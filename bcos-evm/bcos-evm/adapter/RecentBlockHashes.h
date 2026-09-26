// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// RecentBlockHashes — lazy-loading BlockHashes aligned with op-geth's GetHashFn
// (core/evm.go:103-140). The EVM BLOCKHASH opcode only queries the [max(0,N-256), N-1] window
// (evmone instructions.hpp:681-691); outside it the opcode layer returns zero. This type seeds
// {N-1: parentHash} (zero storage reads, covers the first block / EIP-2935) and lazily loads
// earlier ancestors from SYS_NUMBER_2_HASH — equivalent to op-geth's header walk-back on
// reorg-free chains (given a contiguous table). Constructed per block in the execution path
// (OpBlockExecute.h's preBlockOpSteps), lives for the block; relies on the engine's
// x_state-serialized execution segment, so no internal locks.

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/lexical_cast.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>

namespace bcos::evm::engine::detail
{
/// Lazy-loading BlockHashes with op-geth GetHashFn semantics: seed {N-1: parentHash}, earlier
/// ancestors looked up on demand from SYS_NUMBER_2_HASH; missing entries return zero; storage
/// errors / bad value lengths record the poison flag and return zero.
///
/// Error channel note: this class reports through a bare `std::optional<std::string>*`, NOT
/// Storage2State's SharedErrorSlot — the evmone BlockHashes interface is const+noexcept and the
/// instance is constructed by preBlockOpSteps before any executor shared slot exists. The
/// block-level check must read BOTH channels (hashErr here, poisoned() on the bridge).
template <class Storage>
class RecentBlockHashes final : public evmone::state::BlockHashes
{
public:
    RecentBlockHashes(Storage& storage, int64_t blockNumber, evmc::bytes32 parentHash,
        std::optional<std::string>* error)
      : m_storage(storage), m_blockNumber(blockNumber), m_parentHash(parentHash), m_error(error)
    {
        // Seed: the EIP-2935 system call only queries N-1 (system_contracts.cpp:49-53), which the
        // first block also hits. Non-noexcept context (constructor), so emplace needs no try.
        m_cache.emplace(m_blockNumber - 1, m_parentHash);
    }

    evmc::bytes32 get_block_hash(int64_t n) const noexcept override
    {
        // The interface is noexcept, so the whole body is wrapped in try/catch — emplace and
        // the poison write both allocate; an uncaught bad_alloc in a noexcept function would
        // terminate.
        try
        {
            if (n >= m_blockNumber || n < 0)
                return evmc::bytes32{};
            if (const auto it = m_cache.find(n); it != m_cache.end())
                return it->second;

            const auto key = boost::lexical_cast<std::string>(n);
            auto entry = bcos::task::syncWait(bcos::storage2::readOne(
                m_storage, bcos::executor_v1::StateKeyView{bcos::ledger::SYS_NUMBER_2_HASH, key}));
            if (!entry.has_value())
                return evmc::bytes32{};  // missing row = op-geth's pruned/unreachable semantics

            // Value-length validation, mirroring Storage2State::fetchAllStorage.
            const auto value = entry->get();
            if (value.size() != sizeof(evmc::bytes32::bytes))
            {
                poison("RecentBlockHashes: SYS_NUMBER_2_HASH entry length != 32");
                return evmc::bytes32{};
            }
            evmc::bytes32 out{};
            std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
            // emplace inside try: bad_alloc jumps to catch.
            m_cache.emplace(n, out);
            return out;
        }
        catch (...)
        {
            poison("RecentBlockHashes: storage read or cache insert failed");
            return evmc::bytes32{};
        }
    }

private:
    void poison(std::string msg) const noexcept
    {
        // Records only the first error, never rethrows (mirrors Storage2State::poison()).
        try
        {
            if (m_error != nullptr && !*m_error)
                *m_error = std::move(msg);
        }
        catch (...)
        {}
    }

    Storage& m_storage;
    int64_t m_blockNumber = 0;
    evmc::bytes32 m_parentHash{};
    std::optional<std::string>* m_error = nullptr;
    mutable std::map<int64_t, evmc::bytes32> m_cache;  // interface is const noexcept -> mutable
};
}  // namespace bcos::evm::engine::detail
