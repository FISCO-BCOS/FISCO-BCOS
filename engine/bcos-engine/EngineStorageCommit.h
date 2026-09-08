/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-task/Task.h>

namespace bcos::engine::engine_common
{

/// Merge every remaining queued layer. Empty deque is NotExistsImmutableStorageError
/// and ends the loop (A7-1 / A7-2). This still merge-alls; discarding abandoned
/// layers needs a coroutine-safe commit serial (A8-1 / A8-4 / A7-4).
template <class Storage>
task::Task<void> drainQueuedLayers(Storage& storage)
{
    for (;;)
    {
        bool drained = false;
        try
        {
            co_await storage.mergeBackStorage();
            drained = true;
        }
        catch (bcos::storage2::NotExistsImmutableStorageError const&)
        {}
        if (!drained)
        {
            co_return;
        }
    }
}

}  // namespace bcos::engine::engine_common
