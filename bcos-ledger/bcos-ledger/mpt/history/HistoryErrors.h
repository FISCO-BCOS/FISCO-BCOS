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
 * @file HistoryErrors.h
 * @brief Exception types raised by the reverse-history index (spec B.3)
 */
#pragma once

#include <bcos-utilities/Exceptions.h>

namespace bcos::ledger::mpt::history
{

/// The records that would answer the query are gone. Three places raise it, all saying the same
/// thing from different evidence: the window guard spec B.3 requires to run BEFORE the lookup
/// (block < tip - depth + 1), the on-disk retention boundary the index carries, and a located
/// version whose shard row turns out to be deleted.
///
/// The guard has to run first because past the window a lookup cannot tell "this key was never
/// changed after B" from "the record existed and was deleted", and would otherwise return the
/// current value as if it were the historical one — the one silently-wrong answer the whole layout
/// is built to avoid.
DERIVE_BCOS_EXCEPTION(HistoryPruned);

/// The requested block number is not a queryable point on this chain — negative, or ahead of the
/// caller-supplied tip. Distinct from HistoryPruned: that one means "was retained, no longer is",
/// this one means "never was".
DERIVE_BCOS_EXCEPTION(InvalidHistoryBlock);

/// The in-memory query index cannot answer: it has never been rebuilt (IndexState::Empty), or a
/// rebuild or a publish failed and left it IndexState::Unavailable. G10 — a missing index is not
/// evidence that a key went unmodified, so every history read refuses instead of falling through
/// to the current value. Distinct from HistoryPruned, which is a statement about a block the
/// store deliberately dropped; this one says the store does not know what it holds.
DERIVE_BCOS_EXCEPTION(HistoryIndexUnavailable);

}  // namespace bcos::ledger::mpt::history
