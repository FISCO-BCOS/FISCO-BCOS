// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared error types for the OP scheduler. Split out of OpSchedulerImpl.h so the decode
// layers (OpRlpDecode.h / OpTxDecode.h) can throw without depending on the class
// template — and so neither side needs an include-order guarantee.

#include <stdexcept>
#include <string>

namespace bcos::evm::engine
{
/// Thrown for anything OP block execution classifies as a consensus-level rejection (error
/// table): malformed/undecodable raw tx bytes, processOpBlock's own semantic throws
/// (empty block, first tx not the L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
/// on the caller side, never -32603.
struct OpConsensusError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Thrown when the ledger bridge's poison flag is set (a storage2-layer failure, not a consensus
/// violation — Storage2State.h's poison-flag error channel contract). Maps to JSON-RPC -32603
/// internal error on the caller side, never INVALID.
struct OpStorageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};
}  // namespace bcos::evm::engine
