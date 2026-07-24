/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief MPT node-row key layout: the "/mpt/" state table and its encode/decode helpers
 * @file KeyPrefixes.h
 * @author: kyonRay
 * @date: 2026-05-12
 */
#pragma once
#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-utilities/FixedBytes.h>
#include <string_view>

namespace bcos::storage2
{

/// The state TABLE of MPT trie-node rows. A node row is an ordinary state row —
/// StateKey{"/mpt/", <32 raw digest bytes>} — so its physical key in the default
/// ColumnFamily is the StateKey serialization "<table>" ':' "<key>":
///
///   "/mpt/:" + 32 raw digest bytes = 38 bytes
///
/// "/mpt/" itself contains no ':', so the first ':' of every node row's physical key sits
/// at index 5 and StateKeyResolver::decode's split-at-first-colon reconstruction is exact
/// for every digest — including digests that happen to contain 0x3A (':') bytes.
/// Do NOT write "/mpt/" literals anywhere else — build keys with mptNodeStateKey;
/// the physical form is produced and parsed solely by StateKeyResolver.
inline constexpr std::string_view kMPTTable = "/mpt/";
inline constexpr std::size_t kMPTKeyLength = kMPTTable.size() + 1 + 32;  // 38

/// The StateKey of a trie node's row: table "/mpt/", row key = the 32 raw digest bytes.
/// This is the form every in-process reader and writer uses — the ordinary state
/// resolvers and storages handle it with no MPT-specific code.
inline executor_v1::StateKey mptNodeStateKey(const h256& hash)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return {kMPTTable, std::string_view(reinterpret_cast<const char*>(hash.data()), h256::SIZE)};
}

}  // namespace bcos::storage2
