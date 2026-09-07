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
 * @brief MPT node-row key layout: the two path-addressed node tables (pathdb spec §8.2)
 * @file KeyPrefixes.h
 * @author: kyonRay
 * @date: 2026-05-12
 */
#pragma once
#include <string_view>

namespace bcos::storage2
{

/// The two state TABLES of MPT trie-node rows. A node row is an ordinary state row whose row key
/// is the node's POSITION in its trie, so its physical key in the default ColumnFamily is the
/// StateKey serialization "<table>" ':' "<key>":
///
///   "/mptp/a:" + compactPath(position)
///   "/mptp/s:" + 32 raw owner bytes + compactPath(position)
///
/// The trie kind rides on the table name rather than a tag inside the row key. That keeps every
/// node row's row key unambiguously decodable (an owner is fixed-width, a position is not, so a
/// single shared table could not tell the two apart), and it makes one account's whole storage
/// trie a contiguous "/mptp/s:<owner>" key range.
///
/// Neither name contains ':', so the first ':' of every node row's physical key sits at a fixed
/// index — the same one for both tables, since they are the same length — and
/// StateKeyResolver::decode's split-at-first-colon reconstruction is exact for every row key,
/// including positions and owners that contain 0x3A (':') bytes.
///
/// Do NOT write these literals anywhere else — build keys with ledger::mpt::pathNodeStateKey
/// (PathKey.h); the physical form is produced and parsed solely by StateKeyResolver.
inline constexpr std::string_view kMPTAccountTable = "/mptp/a";
inline constexpr std::string_view kMPTStorageTable = "/mptp/s";

static_assert(kMPTAccountTable.find(':') == std::string_view::npos &&
                  kMPTStorageTable.find(':') == std::string_view::npos,
    "an MPT node table name must not contain ':' — StateKeyResolver splits a physical key at its "
    "FIRST colon, so a colon in the table name would decode node rows to a corrupted "
    "table/key split");
static_assert(kMPTAccountTable.size() == kMPTStorageTable.size(),
    "the two MPT node table names must be the same length, so the ':' StateKeyResolver inserts "
    "sits at the same offset for both");
static_assert(kMPTAccountTable != kMPTStorageTable,
    "the account and storage node tables must be distinct namespaces");

}  // namespace bcos::storage2
