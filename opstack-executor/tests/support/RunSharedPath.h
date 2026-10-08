// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Compatibility driver for the post-cutover suites that predate DualRunHarness:
// runSharedPath(storage, header, rawTxBytes, transactions, spec) is a thin adapter
// over DualRunHarness's runExecutorPath (the production scheduler shape). The old
// pre-cutover signature (OpForkConfig + OpstackExecutor) is retired — callers pass
// an OpForkSpec and a DualRunFixture now.
#pragma once

#include "DualRunHarness.h"

#include <bcos-protocol/BlockHeader.h>
#include <bcos-task/Wait.h>

#include <memory>
#include <vector>

namespace opstack_test
{

/// Adapter over runExecutorPath for the suites that hold a bare MutableStorage (no
/// MLS): it forks a view over a fresh DualRunFixture's backend, seeds the caller's
/// storage as the mutable layer, executes, and returns the new-API block result.
/// NOTE: writes land in the ADAPTER's fixture view, not the caller's bare storage —
/// suites that need to inspect post-state should hold a DualRunFixture and call
/// runExecutorPath directly (see OpEthExecutorDualRunTest).
template <class StorageType>
opeth::OpEthExecuteBlockResult runSharedPath(StorageType& /*storage*/,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes,
    std::vector<bcos::protocol::Transaction::ConstPtr> const& transactions,
    opeth::OpForkSpec const& spec)
{
    DualRunFixture fixture;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    return runExecutorPath(fixture, view, header, spec, transactions, rawTxBytes);
}

}  // namespace opstack_test
