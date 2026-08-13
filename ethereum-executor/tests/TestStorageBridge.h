/// @file TestStorageBridge.h
/// @brief Forwarders kept for the EEST runner: the BCOS-storage <->
///        evmone::state::StateView / StateDiff bridge that used to live here
///        test-only was promoted to the executor library when the OP Stack
///        execution lane made it production code (see
///        ethereum-executor/OpStackBridge.h). The runner keeps its old names.

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "ethereum-executor/OpStackBridge.h"

namespace bcos::test
{

template <class Storage>
using TestStorageStateView = bcos::executor_v1::eth::StorageStateView<Storage>;

/// The hashImpl parameter is retained for signature compatibility but unused:
/// the production apply hashes code with keccak256 unconditionally (Ethereum
/// consensus hashing), which is what every EEST call site passed anyway.
template <class Storage>
task::Task<void> testApplyStateDiff(
    Storage& storage, evmone::state::StateDiff const& diff, crypto::Hash const& /*hashImpl*/)
{
    co_await bcos::executor_v1::eth::applyEvmoneStateDiff(storage, diff);
}

}  // namespace bcos::test
