// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared error types + the block-execution result for the OP scheduler. Split out of
// OpSchedulerImpl.h so the decode layers (OpRlpDecode.h / OpTxDecode.h) can throw without
// depending on the class template — and so neither side needs an include-order guarantee.

#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockSeal.h>
#include <stdexcept>
#include <string>
#include <vector>

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

/// Six-way comparison surface for an executed OP block: `seal`'s
/// receiptsRoot/logsBloom/withdrawalsRoot (bcos::evm::opstack::OpBlockSeal, unchanged structure)
/// plus three members below (stateRoot/gasUsed/txRoot) that are deliberately NOT folded into
/// OpBlockSeal.
struct OpExecuteBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;
    bcos::h256 stateRoot;
    uint64_t gasUsed;
    bcos::h256 txRoot;
};
}  // namespace bcos::evm::engine
