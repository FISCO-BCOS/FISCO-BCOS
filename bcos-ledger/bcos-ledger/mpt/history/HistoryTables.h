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
 * @file HistoryTables.h
 * @brief The four state tables the reverse-history index lives in (spec B.2, B.8)
 */
#pragma once

#include <string_view>

namespace bcos::ledger::mpt::history
{

/// The pair of state tables one reverse-history instance owns: the (key, block)-ordered index
/// that answers point queries, and the (block, shard)-ordered manifest that answers whole-block
/// traversal. Spec B.1: one sort order cannot serve both access patterns, so the same diff is
/// stored twice — values only in the index, keys only in the manifest (B.2(a)).
struct HistoryTables
{
    std::string_view index;
    std::string_view manifest;
};

/// State history: the pre-images of ordinary state rows, retained for H_state blocks.
inline constexpr HistoryTables kStateHistory{.index = "/mptsi/", .manifest = "/mpths/"};

/// Trie history: the pre-images of path-addressed trie-node rows, retained for H_proof blocks.
/// Same mechanism, second instantiation (spec B.8).
inline constexpr HistoryTables kTrieHistory{.index = "/mptti/", .manifest = "/mptth/"};

/// A table name must not contain ':': StateKeyResolver splits a physical key at its FIRST colon
/// to rebuild the (table, rowKey) pair, so a colon in the table name would corrupt the split.
/// History row keys DO contain arbitrary bytes (0x3A among them) — that is safe precisely
/// because every colon in them sits after the first one. Same rule and reasoning as
/// bcos-storage/KeyPrefixes.h:43.
static_assert(kStateHistory.index.find(':') == std::string_view::npos &&
                  kStateHistory.manifest.find(':') == std::string_view::npos &&
                  kTrieHistory.index.find(':') == std::string_view::npos &&
                  kTrieHistory.manifest.find(':') == std::string_view::npos,
    "a history table name must not contain ':'");

/// The four tables must stay distinct: rows are located by seeking into a table and walking
/// forward until the table changes, so a shared name would let one instance's scan consume the
/// other's rows. All four names are the same length, so distinctness also rules out one being a
/// prefix of another.
static_assert(kStateHistory.index.size() == kStateHistory.manifest.size() &&
                  kStateHistory.index.size() == kTrieHistory.index.size() &&
                  kStateHistory.index.size() == kTrieHistory.manifest.size(),
    "the four history table names are asserted distinct below on the strength of being equal "
    "length; if that stops holding, the prefix case needs its own check");
static_assert(kStateHistory.index != kStateHistory.manifest &&
                  kStateHistory.index != kTrieHistory.index &&
                  kStateHistory.index != kTrieHistory.manifest &&
                  kStateHistory.manifest != kTrieHistory.index &&
                  kStateHistory.manifest != kTrieHistory.manifest &&
                  kTrieHistory.index != kTrieHistory.manifest,
    "the four history table names must be distinct");

}  // namespace bcos::ledger::mpt::history
