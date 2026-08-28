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
 * @file MemPoolTryAddTest.cpp
 * @brief Exit contract of MemPoolImpl::tryAdd (every arm, including the four that add()
 *        reports silently) plus the concurrent same-(sender, nonce) case.
 */

#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos::test
{
// Distinct names throughout: this file is unity-built into the same translation unit as
// MemPoolImplTest.cpp, so an anonymous namespace would not isolate it.
class TryAddTransaction : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
    void markTainted() { setTainted(true); }
};

static bytes tryAddToBytes(std::string_view str)
{
    return {reinterpret_cast<const byte*>(str.data()),
        reinterpret_cast<const byte*>(str.data()) + str.size()};
}

/// A BCOS transaction with a computed dataHash, untainted, ready to be admitted.
///
/// @p payload distinguishes two transactions that share a (sender, nonce). It has to be applied
/// before calculateHash: TarsHashable.h returns the cached dataHash once it is set, so mutating
/// the struct and re-hashing afterwards leaves the hash untouched -- and two same-hash
/// transactions collide in the hash index, never reaching the (sender, nonce) index this suite
/// is about.
static std::shared_ptr<TryAddTransaction> tryAddMakeTx(
    std::string_view sender, int64_t nonce, std::string_view payload = {})
{
    auto tx = std::make_shared<TryAddTransaction>();
    tx->mutableInner().data.to.assign(sender.begin(), sender.end());
    tx->mutableInner().data.input.assign(payload.begin(), payload.end());
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(tryAddToBytes(sender));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(nonce);
    return tx;
}

BOOST_AUTO_TEST_SUITE(MemPoolTryAddTest)

BOOST_AUTO_TEST_CASE(admittedReturnsNone)
{
    MemPoolImpl pool;
    BOOST_CHECK(pool.tryAdd(tryAddMakeTx("sender_aaaaaaaaaaaa", 1)) == TransactionStatus::None);
}

BOOST_AUTO_TEST_CASE(nullTransactionThrows)
{
    MemPoolImpl pool;
    // add() returns silently here; tryAdd reports it, because a null pointer is a caller bug
    // and must not be indistinguishable from a rejected transaction.
    BOOST_CHECK_THROW(pool.tryAdd(nullptr), NullTransaction);
}

BOOST_AUTO_TEST_CASE(taintedTransactionThrows)
{
    MemPoolImpl pool;
    auto tx = tryAddMakeTx("sender_bbbbbbbbbbbb", 1);
    tx->markTainted();  // signature not verified yet
    BOOST_CHECK_THROW(pool.tryAdd(tx), InvalidTaintedTransaction);
}

BOOST_AUTO_TEST_CASE(blobEnvelopeThrows)
{
    MemPoolImpl pool;
    auto tx = tryAddMakeTx("sender_cccccccccccc", 1);
    tx->mutableInner().type = static_cast<char>(TransactionType::Web3Transaction);
    // EIP-2718 type byte 0x03 = blob (EIP-4844); never admissible on this chain.
    tx->mutableInner().extraTransactionBytes = {0x03, 0x01, 0x02};
    tx->mutableInner().extraTransactionHash.assign(32, 0x11);
    BOOST_CHECK_THROW(pool.tryAdd(tx), InvalidBlobTransaction);
}

BOOST_AUTO_TEST_CASE(unhashableTransactionReturnsMalformed)
{
    MemPoolImpl pool;
    auto tx = std::make_shared<TryAddTransaction>();
    tx->setNonce("1");
    tx->forceSender(tryAddToBytes("sender_dddddddddddd"));
    tx->markClean();
    // Neither dataHash nor extraTransactionHash set -> hash() throws EmptyTransactionHash.
    // add() swallows this and returns, so its caller cannot tell the transaction was dropped.
    BOOST_CHECK(pool.tryAdd(tx) == TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(duplicateHashReturnsAlreadyInTxPool)
{
    MemPoolImpl pool;
    auto first = tryAddMakeTx("sender_eeeeeeeeeeee", 1);
    BOOST_CHECK(pool.tryAdd(first) == TransactionStatus::None);
    // Same bytes -> same dataHash.
    BOOST_CHECK(
        pool.tryAdd(tryAddMakeTx("sender_eeeeeeeeeeee", 1)) == TransactionStatus::AlreadyInTxPool);
}

BOOST_AUTO_TEST_CASE(nonceConflictRejectsAndKeepsTheResident)
{
    MemPoolImpl pool;
    auto resident = tryAddMakeTx("sender_ffffffffffff", 7);
    BOOST_CHECK(pool.tryAdd(resident) == TransactionStatus::None);

    // Same (sender, nonce), different content -> different hash, so this is not caught by the
    // hash index. add() would replace the resident; tryAdd rejects, first come first served.
    auto challenger = tryAddMakeTx("sender_ffffffffffff", 7, "challenger");
    BOOST_REQUIRE(challenger->hash() != resident->hash());

    BOOST_CHECK(pool.tryAdd(challenger) == TransactionStatus::NonceCheckFail);

    // The resident is still the one in the pool. get() returns one slot per requested hash and
    // leaves it null on a miss, so absence is `== nullptr`, not an empty vector.
    auto got = pool.get(std::vector<HashType>{resident->hash(), challenger->hash()});
    BOOST_REQUIRE_EQUAL(got.size(), 2U);
    BOOST_REQUIRE(got[0] != nullptr);
    BOOST_CHECK(got[0]->hash() == resident->hash());
    BOOST_CHECK(got[1] == nullptr);
}

BOOST_AUTO_TEST_CASE(unparsableNonceReturnsNonceCheckFail)
{
    MemPoolImpl pool;
    auto tx = tryAddMakeTx("sender_gggggggggggg", 1);
    tx->setNonce("not-a-number");
    BOOST_CHECK(pool.tryAdd(tx) == TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(addStillReplacesOnNonceConflict)
{
    // Guard: tryAdd's reject-on-conflict rule must not have leaked into add(), whose existing
    // in-process callers (EngineServiceImpl) rely on replacement.
    MemPoolImpl pool;
    auto resident = tryAddMakeTx("sender_hhhhhhhhhhhh", 3);
    auto challenger = tryAddMakeTx("sender_hhhhhhhhhhhh", 3, "challenger");
    BOOST_REQUIRE(challenger->hash() != resident->hash());

    pool.add(std::vector<Transaction::Ptr>{resident});
    pool.add(std::vector<Transaction::Ptr>{challenger});

    auto got = pool.get(std::vector<HashType>{resident->hash(), challenger->hash()});
    BOOST_REQUIRE_EQUAL(got.size(), 2U);
    BOOST_CHECK(got[0] == nullptr);  // replaced
    BOOST_REQUIRE(got[1] != nullptr);
    BOOST_CHECK(got[1]->hash() == challenger->hash());
}

BOOST_AUTO_TEST_CASE(concurrentSameSenderNonceAdmitsExactlyOne)
{
    // The check-and-insert has to be one critical section. "Query the pool, then call the void
    // add()" is check-then-act: both threads pass the query and the second silently replaces
    // the first, so counting pool entries alone (1 either way) cannot tell the two apart --
    // the assertion that matters is that exactly one caller was told NonceCheckFail.
    constexpr int kRounds = 200;
    for (int round = 0; round < kRounds; ++round)
    {
        MemPoolImpl pool;
        auto sender = "sender_conc_" + std::to_string(round);
        auto left = tryAddMakeTx(sender, 5, "left");
        auto right = tryAddMakeTx(sender, 5, "right");
        // Distinct hashes on purpose: with equal hashes the loser would be rejected by the hash
        // index instead of the (sender, nonce) index, and this case would pass without ever
        // exercising the atomic check-and-reserve it is meant to cover.
        BOOST_REQUIRE(left->hash() != right->hash());

        std::atomic<int> admitted{0};
        std::atomic<int> rejected{0};
        auto submit = [&](Transaction::Ptr tx) {
            if (pool.tryAdd(std::move(tx)) == TransactionStatus::None)
            {
                ++admitted;
            }
            else
            {
                ++rejected;
            }
        };
        std::thread t1(submit, left);
        std::thread t2(submit, right);
        t1.join();
        t2.join();

        BOOST_REQUIRE_EQUAL(admitted.load(), 1);
        BOOST_REQUIRE_EQUAL(rejected.load(), 1);
    }
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
