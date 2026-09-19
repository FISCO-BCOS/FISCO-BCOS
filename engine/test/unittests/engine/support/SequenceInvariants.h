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
 * @file SequenceInvariants.h
 * @brief I1-I6 canonicality invariants for the M4 import/FCU sequence matrix.
 */

// I1-I6: the canonicality invariants every import/FCU sequence must preserve after
// every step. Derived from the S5+S6 design §4.2/§4.4.x and the defects found by the
// milestone review (N1/N2/N3/NEW-1/NEW-2/NEW-3); these are the "guarding格" of the
// M4 sequence matrix.
#pragma once

#include <bcos-framework/ledger/Ledger.h>         // fromStorage
#include <bcos-framework/ledger/LedgerTypeDef.h>  // HEADER / TRANSACTIONS
#include <bcos-framework/protocol/Block.h>        // Block::transactionsSize
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-task/Wait.h>             // syncWait
#include <bcos-utilities/FixedBytes.h>  // h256

#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace op_matrix
{
/// I1: number->hash and hash->number agree for every canonical height. (N2/NEW-1)
template <class StorageType>
void checkNumberHashAgreement(StorageType& storage, bcos::protocol::BlockNumber head)
{
    auto view = storage.forkCommitted();
    for (bcos::protocol::BlockNumber n = 0; n <= head; ++n)
    {
        auto hash =
            bcos::task::syncWait(bcos::ledger::getBlockHash(view, n, bcos::ledger::fromStorage));
        BOOST_REQUIRE_MESSAGE(hash.has_value(), fmt::format("I1: no hash at height {}", n));
        auto number = bcos::task::syncWait(
            bcos::ledger::getBlockNumber(view, *hash, bcos::ledger::fromStorage));
        BOOST_REQUIRE_MESSAGE(number.has_value(), fmt::format("I1: hash at {} unresolvable", n));
        BOOST_CHECK_EQUAL(*number, n);
    }
}

/// I2: the by-number transaction list is resolvable and has the head's tx count. (N1)
/// NOTE: the storage overload of ledger::getBlockData takes the blockFactory as its 4th
/// argument (LedgerMethods.h) -- see the existing CanonicalImportedBlockHasNumberToTxsRow call.
template <class StorageType>
void checkByNumberTransactions(StorageType& storage, bcos::protocol::BlockFactory& blockFactory,
    bcos::protocol::BlockNumber head, std::size_t expectedTxs)
{
    auto view = storage.forkCommitted();
    auto block = bcos::task::syncWait(bcos::ledger::getBlockData(
        view, head, bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS, blockFactory));
    BOOST_REQUIRE(block != nullptr);
    BOOST_CHECK_EQUAL(block->transactionsSize(), expectedTxs);
}

/// I3: nothing above the head resolves by number. (NEW-1/N2)
template <class StorageType>
void checkNoRowsAboveHead(StorageType& storage, bcos::protocol::BlockNumber head)
{
    auto view = storage.forkCommitted();
    for (bcos::protocol::BlockNumber n = head + 1; n <= head + 3; ++n)
    {
        BOOST_CHECK_MESSAGE(
            !bcos::task::syncWait(bcos::ledger::getBlockHash(view, n, bcos::ledger::fromStorage))
                 .has_value(),
            fmt::format("I3: height {} still resolves above head {}", n, head));
    }
}

/// I4: hashes that were de-canonicalized must not resolve any more. (NEW-1/NEW-2)
template <class StorageType>
void checkHashesUnresolvable(StorageType& storage, std::vector<bcos::h256> const& deadHashes)
{
    auto view = storage.forkCommitted();
    for (auto const& h : deadHashes)
    {
        BOOST_CHECK_MESSAGE(
            !bcos::task::syncWait(bcos::ledger::getBlockNumber(view, h, bcos::ledger::fromStorage))
                 .has_value(),
            fmt::format("I4: de-canonicalized hash {} still resolves", h.hex()));
    }
}

/// I5: the committed tip pointer equals the announced head. (post-condition proxy:
/// the real state-root check runs inside canonicalize and throws on mismatch.)
template <class StorageType>
void checkTipIs(StorageType& storage, bcos::protocol::BlockNumber expectedTip)
{
    auto view = storage.forkCommitted();
    auto const tip =
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
    BOOST_CHECK_EQUAL(tip, static_cast<int64_t>(expectedTip));
}

/// I6: the tip never moves backwards unless the scenario says so.
inline void checkTipMonotonic(
    bcos::protocol::BlockNumber previous, bcos::protocol::BlockNumber current, bool allowRewind)
{
    if (!allowRewind)
    {
        BOOST_CHECK_MESSAGE(
            current >= previous, fmt::format("I6: tip rewound {} -> {}", previous, current));
    }
}

/// What a single sequence step must satisfy; every sequence builds one of these after
/// each step and calls checkAll, so I1-I6 really run everywhere (spec §9 P1).
struct StepExpectations
{
    bcos::protocol::BlockNumber previousTip{};
    bcos::protocol::BlockNumber head{};
    std::size_t headTxCount{};
    std::vector<bcos::h256> deadHashes{};  // I4: hashes de-canonicalized by this step
    bool allowRewind = false;              // I6: true only for S5/S14-style rewinds
};

template <class StorageType>
void checkAll(StorageType& storage, bcos::protocol::BlockFactory& blockFactory,
    StepExpectations const& expectations)
{
    checkTipMonotonic(expectations.previousTip, expectations.head, expectations.allowRewind);
    checkTipIs(storage, expectations.head);
    checkNumberHashAgreement(storage, expectations.head);
    checkByNumberTransactions(storage, blockFactory, expectations.head, expectations.headTxCount);
    checkNoRowsAboveHead(storage, expectations.head);
    if (!expectations.deadHashes.empty())
    {
        checkHashesUnresolvable(storage, expectations.deadHashes);
    }
}
}  // namespace op_matrix
