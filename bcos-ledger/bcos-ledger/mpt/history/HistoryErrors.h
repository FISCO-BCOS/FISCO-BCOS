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

/// The requested block has fallen out of the retention window (block < tip - depth + 1), so the
/// index rows that would answer the query have been expired away. Raised by the window guard
/// that spec B.3 requires to run BEFORE the seek: past the window the seek cannot tell "this key
/// was never changed after B" from "the record existed and was deleted", and would otherwise
/// return the current value as if it were the historical one — the one silently-wrong answer the
/// whole layout is built to avoid.
DERIVE_BCOS_EXCEPTION(HistoryPruned);

/// The requested block number is not a queryable point on this chain — negative, or ahead of the
/// caller-supplied tip. Distinct from HistoryPruned: that one means "was retained, no longer is",
/// this one means "never was".
DERIVE_BCOS_EXCEPTION(InvalidHistoryBlock);

}  // namespace bcos::ledger::mpt::history
