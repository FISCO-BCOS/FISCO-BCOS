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
 * @file FIB167_FillBlockBoundTest.cpp
 * @brief Regression test for FIB-167: TxPool::fillBlock() must reject caller-controlled
 *        txsHash inputs larger than the per-block transaction limit so that a peer
 *        cannot trigger unbounded CPU/memory work or amplify a ledger-fetch storm.
 */

#include "test/unittests/txpool/TxPoolFixture.h"
#include <boost/test/unit_test.hpp>
#include <future>

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(FIB167_FillBlockBound, TxPoolFixture)

BOOST_AUTO_TEST_CASE(fillBlockRejectsOversizedTxsHash)
{
    // FIB-167: fillBlock must reject txsHash sizes greater than blockTxCountLimit.
    constexpr size_t blockTxLimit = 100;
    constexpr size_t oversizeCount = blockTxLimit + 1;  // smallest oversize, sufficient

    auto& txpool = dynamic_cast<bcos::txpool::TxPool&>(*this->txpool());
    txpool.notifyBlockTxCountLimit(blockTxLimit);

    auto oversize = std::make_shared<bcos::crypto::HashList>();
    oversize->reserve(oversizeCount);
    for (size_t i = 0; i < oversizeCount; ++i)
    {
        oversize->emplace_back(bcos::crypto::HashType::generateRandomFixedBytes());
    }

    std::promise<std::pair<bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr>> donePromise;
    auto doneFuture = donePromise.get_future();

    txpool.asyncFillBlock(
        oversize, [&](bcos::Error::Ptr err, bcos::protocol::ConstTransactionsPtr txs) {
            donePromise.set_value({std::move(err), std::move(txs)});
        });

    auto [err, txs] = doneFuture.get();
    BOOST_REQUIRE(err);
    BOOST_CHECK(!txs);
    // FIB-167 rejection -- the message must mention the size cap, not just be a generic
    // missing-tx error.
    BOOST_CHECK_NE(err->errorMessage().find("exceeds blockTxCountLimit"), std::string::npos);
}

BOOST_AUTO_TEST_CASE(fillBlockAcceptsSizeAtLimit)
{
    constexpr size_t blockTxLimit = 100;
    auto& txpool = dynamic_cast<bcos::txpool::TxPool&>(*this->txpool());
    txpool.notifyBlockTxCountLimit(blockTxLimit);

    auto atLimit = std::make_shared<bcos::crypto::HashList>();
    atLimit->reserve(blockTxLimit);
    for (size_t i = 0; i < blockTxLimit; ++i)
    {
        atLimit->emplace_back(bcos::crypto::HashType::generateRandomFixedBytes());
    }

    std::promise<std::pair<bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr>> donePromise;
    auto doneFuture = donePromise.get_future();

    // size == limit must NOT be rejected by FIB-167's bound check. The hashes don't
    // exist in the pool, so the callback may still fire with a TransactionsMissing error
    // from the missed-tx-from-ledger path, but the rejection MUST NOT carry the bound
    // message.
    txpool.asyncFillBlock(
        atLimit, [&](bcos::Error::Ptr err, bcos::protocol::ConstTransactionsPtr txs) {
            donePromise.set_value({std::move(err), std::move(txs)});
        });

    auto [err, txs] = doneFuture.get();
    if (err)
    {
        BOOST_CHECK_EQUAL(err->errorMessage().find("exceeds blockTxCountLimit"), std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(fillBlockBoundFollowsLiveLimitUpdate)
{
    // FIB-167: after notifyBlockTxCountLimit lifts the cap (mirroring a system-contract
    // governance bump propagated through PBFTInitializer's new-block notifier), a request
    // that was previously oversized must no longer trip the bound check. This is the
    // invariant the init-time-snapshot approach broke and the reason the limit lives on
    // an atomic field that gets refreshed on every committed block.
    auto& txpool = dynamic_cast<bcos::txpool::TxPool&>(*this->txpool());

    constexpr size_t lowLimit = 50;
    constexpr size_t hashCount = 150;
    constexpr size_t highLimit = 200;

    auto hashes = std::make_shared<bcos::crypto::HashList>();
    hashes->reserve(hashCount);
    for (size_t i = 0; i < hashCount; ++i)
    {
        hashes->emplace_back(bcos::crypto::HashType::generateRandomFixedBytes());
    }

    // Phase 1: limit below request size -- bound check must reject.
    txpool.notifyBlockTxCountLimit(lowLimit);
    {
        std::promise<bcos::Error::Ptr> rejectPromise;
        auto rejectFuture = rejectPromise.get_future();
        txpool.asyncFillBlock(
            hashes, [&](bcos::Error::Ptr err, auto&&) { rejectPromise.set_value(std::move(err)); });
        auto err = rejectFuture.get();
        BOOST_REQUIRE(err);
        BOOST_CHECK_NE(err->errorMessage().find("exceeds blockTxCountLimit"), std::string::npos);
    }

    // Phase 2: limit raised above request size -- bound check must NOT trip. A downstream
    // missing-tx error is allowed (hashes don't exist), but the message must not carry the
    // bound-rejection tag.
    txpool.notifyBlockTxCountLimit(highLimit);
    {
        std::promise<bcos::Error::Ptr> acceptPromise;
        auto acceptFuture = acceptPromise.get_future();
        txpool.asyncFillBlock(
            hashes, [&](bcos::Error::Ptr err, auto&&) { acceptPromise.set_value(std::move(err)); });
        auto err = acceptFuture.get();
        if (err)
        {
            BOOST_CHECK_EQUAL(
                err->errorMessage().find("exceeds blockTxCountLimit"), std::string::npos);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
