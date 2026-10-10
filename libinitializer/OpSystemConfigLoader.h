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
 * @file OpSystemConfigLoader.h
 * @brief The production IL2ConfigLoader for the OP lane: L2ConfigLoaderImpl over a reader
 *        that forks a fresh COMMITTED view of the GlobalStateStorage per call.
 */
#pragma once

#include "GlobalStateStorageInitializer.h"
#include <bcos-framework/ledger/IL2ConfigLoader.h>
#include <bcos-framework/ledger/L2ConfigLoader.h>
#include <bcos-framework/ledger/L2SystemConfigTable.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <optional>
#include <utility>
#include <vector>

namespace bcos::initializer
{
/// Satisfies L2ConfigLoaderImpl's readSome concept against the committed plane of the
/// GlobalStateStorage. Each call forks its own view (cache -> committed backend, no in-flight
/// pending layers) so the loader observes exactly the state the last durable commit left --
/// the same plane the ledger's getLedgerConfig reads its SYS_CONFIG rows from. Forking per
/// call is what keeps the loader stateless: the view is a cheap handle and the call is once
/// per commit.
struct CommittedStateSlotReader
{
    GlobalStateStorage* storage;

    task::Task<std::vector<std::optional<executor_v1::StateValue>>> readSome(
        std::vector<executor_v1::StateKey> keys)
    {
        auto view = storage->forkCommitted();
        co_return co_await view.readSome(std::move(keys));
    }
};

/// The loader the initializer builds once in OP mode and uses for both the boot load and
/// every post-commit republish (engine/bcos-engine/OpLedgerConfigRepublish.h).
class OpSystemConfigLoader final : public bcos::ledger::IL2ConfigLoader
{
public:
    explicit OpSystemConfigLoader(GlobalStateStorage& storage)
      : m_reader{&storage}, m_impl(m_reader, bcos::ledger::l2SystemConfigTableName())
    {}

    task::Task<void> loadIntoLedgerConfig(
        protocol::BlockNumber blockNumber, bcos::ledger::LedgerConfig& out) override
    {
        return m_impl.loadIntoLedgerConfig(blockNumber, out);
    }

private:
    CommittedStateSlotReader m_reader;
    bcos::ledger::L2ConfigLoaderImpl<CommittedStateSlotReader> m_impl;
};
}  // namespace bcos::initializer
