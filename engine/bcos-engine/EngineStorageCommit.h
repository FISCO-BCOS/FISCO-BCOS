/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EngineStorageCommit.h
 * @brief Shared engine commit helpers (queued-layer drain) used by the Eth/Op services.
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
