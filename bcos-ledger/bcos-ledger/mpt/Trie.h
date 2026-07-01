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
 * @file Trie.h
 * @brief Read-only walk over an MPT given its root hash (spec §5.6, §7.1)
 */
#pragma once
#include "NodeCache.h"
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <functional>
#include <optional>

namespace bcos::ledger::mpt
{
/// Read-only walk over an MPT given its root hash. Nodes are fetched from NodeCache (the caller is
/// responsible for having populated/prefetched it — e.g. HashBuilder::commit writes the nodes it
/// creates). A cache miss for a referenced hash is a programming error and throws.
class Trie
{
public:
    Trie(NodeCache& cache, bcos::h256 root);
    bcos::task::Task<std::optional<bcos::bytes>> get(bcos::h256 const& keyHash) const;
    bcos::h256 root() const noexcept { return m_root; }

private:
    std::reference_wrapper<NodeCache> m_cache;
    bcos::h256 m_root;
};
}  // namespace bcos::ledger::mpt
