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
 * @file E2eCheckpointStorage.h
 * @brief The ONE checkpoint-storage shim the e2e fixtures share (review F26):
 *        before this header ten files carried near-identical private copies whose
 *        only differences were cosmetic (a noexcept, a ref-qualifier, unused-arg
 *        markers). The per-file storage types stay local — only the shim is
 *        single-homed. The shim answers the bcos::storage2 checkpoint concept
 *        trivially: the wrapped storage IS the live plane, checkpoints are no-ops,
 *        and the "open an old checkpoint" entry aborts because no caller may ask.
 */

#pragma once

#include <bcos-framework/storage2/Storage.h>  // ReadWriteStorage concept
#include <bcos-utilities/FixedBytes.h>        // bcos::h256

#include <cstdlib>
#include <optional>

namespace opstack_test
{
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};
}  // namespace opstack_test
