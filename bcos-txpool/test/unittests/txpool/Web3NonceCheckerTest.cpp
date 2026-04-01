/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Web3NonceCheckerTest.cpp
 * @author: kyonGuo
 * @date 2025/3/12
 */

#include "bcos-utilities/Common.h"
#include "test/unittests/txpool/TxPoolFixture.h"
#include <bcos-txpool/txpool/validator/Web3NonceChecker.h>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <memory>
#include <range/v3/all.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>
#include <ranges>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace std::string_view_literals;

namespace bcos::test
{
class Web3NonceCheckerFixture : public TxPoolFixture
{
public:
    Web3NonceCheckerFixture() : TxPoolFixture(), checker(m_ledger) {}
    auto nonceMaker(uint64_t totalSize = 100, uint64_t senderSize = 10, uint64_t duplicateSize = 0)
    {
        std::vector<std::pair<std::string, std::string>> nonceMap = {};
        std::vector<std::string> senders = {};
        for (uint64_t i = 0; i < senderSize; ++i)
        {
            senders.push_back(Address::generateRandomFixedBytes().toRawString());
        }
        for (uint64_t i = 0; i < totalSize / senderSize; ++i)
        {
            for (uint64_t j = 0; j < senderSize; j++)
            {
                auto nonce = FixedBytes<4>::generateRandomFixedBytes().hexPrefixed();
                nonceMap.emplace_back(senders[j], std::move(nonce));
            }
        }
        for (uint64_t i = 0; i < duplicateSize; ++i)
        {
            nonceMap.emplace_back(
                nonceMap.at(FixedBytes<1>::generateRandomFixedBytes()[0] % nonceMap.size()));
        }
        return std::make_tuple(std::move(senders), std::move(nonceMap));
    }
    Web3NonceChecker checker;
};
BOOST_FIXTURE_TEST_SUITE(Web3NonceTest, Web3NonceCheckerFixture)
BOOST_AUTO_TEST_CASE(testNormalFlow)
{
    const auto&& [senders, nonces] = nonceMaker(1000);
    // in txpool
    for (const auto& [sender, nonce] : nonces)
    {
        auto status = task::syncWait(checker.checkWeb3Nonce(sender, nonce));
        BOOST_CHECK_EQUAL(status, TransactionStatus::None);
        BOOST_CHECK(task::syncWait(checker.insertMemoryNonce(sender, nonce)));
    }
    // commit
    std::unordered_map<std::string, std::set<u256>> commitMap = {};
    for (const auto& [sender, nonce] : nonces)
    {
        auto nonceU256 = hex2u(nonce);
        if (auto it = commitMap.find(sender); it != commitMap.end())
        {
            it->second.insert(nonceU256);
        }
        else
        {
            commitMap.insert({sender, {nonceU256}});
        }
    }
    task::syncWait(checker.updateNonceCache(::ranges::views::all(commitMap)));

    for (auto&& sender : senders)
    {
        auto nonce = task::syncWait(checker.getPendingNonce(toHex(sender)));
        BOOST_CHECK(nonce.has_value());
        BOOST_CHECK_EQUAL(nonce.value(), *commitMap[sender].rbegin() + 1);
    }

    // new pending
    for (auto&& sender : senders)
    {
        BOOST_CHECK(task::syncWait(
            checker.insertMemoryNonce(sender, (*commitMap[sender].rbegin() + 2).str())));
        auto nonce = task::syncWait(checker.getPendingNonce(toHex(sender)));
        BOOST_CHECK(nonce.has_value());
        BOOST_CHECK_EQUAL(nonce.value(), *commitMap[sender].rbegin() + 2 + 1);
    }

    // new sender
    {
        auto&& newSender = Address::generateRandomFixedBytes().toRawString();
        auto nonce = task::syncWait(checker.getPendingNonce(toHex(newSender)));
        BOOST_CHECK(!nonce.has_value());

        BOOST_CHECK(task::syncWait(checker.insertMemoryNonce(newSender, "1")));
        nonce = task::syncWait(checker.getPendingNonce(toHex(newSender)));
        BOOST_CHECK(nonce.has_value());
        BOOST_CHECK_EQUAL(nonce.value(), 2);
    }
}

BOOST_AUTO_TEST_CASE(testInvalidNonce)
{
    // test memory layer check
    constexpr uint64_t dupSize = 10;
    const auto&& [senders, nonces] = nonceMaker(100, 10, dupSize);
    // in txpool
    uint64_t dupCount = 0;
    for (const auto& [sender, nonce] : nonces)
    {
        if (const auto status = task::syncWait(checker.checkWeb3Nonce(sender, nonce));
            status == TransactionStatus::NonceCheckFail)
        {
            dupCount++;
        }
        else
        {
            BOOST_CHECK(task::syncWait(checker.insertMemoryNonce(sender, nonce)));
        }
    }
    BOOST_CHECK_EQUAL(dupCount, dupSize);
}

BOOST_AUTO_TEST_CASE(testLedgerNonce)
{
    // test ledger layer check
    constexpr uint64_t totalSize = 1000;
    const auto&& [senders, nonces] = nonceMaker(totalSize);
    std::unordered_map<std::string, std::set<u256>> commitMap = {};
    for (const auto& [sender, nonce] : nonces)
    {
        auto nonceU256 = hex2u(nonce);
        if (auto it = commitMap.find(sender); it != commitMap.end())
        {
            it->second.insert(nonceU256);
        }
        else
        {
            commitMap.insert({sender, {nonceU256}});
        }
    }
    task::syncWait(checker.updateNonceCache(::ranges::views::all(commitMap)));

    uint64_t errorCount = 0;
    for (const auto& [sender, nonce] : nonces)
    {
        const auto status =
            task::syncWait(checker.checkWeb3Nonce(sender, nonce, true /*only check ledger*/));
        if (status != TransactionStatus::None)
        {
            errorCount++;
        }
    }
    BOOST_CHECK_EQUAL(errorCount, totalSize);
}

BOOST_AUTO_TEST_CASE(testStorageLayerNonce)
{
    constexpr uint64_t totalSize = 1000;
    const auto&& [senders, nonces] = nonceMaker(totalSize);
    std::unordered_map<std::string, std::set<u256>> commitMap = {};
    for (const auto& [sender, nonce] : nonces)
    {
        auto nonceU256 = hex2u(nonce);
        if (auto it = commitMap.find(sender); it != commitMap.end())
        {
            it->second.insert(nonceU256);
        }
        else
        {
            commitMap.insert({sender, {nonceU256}});
        }
    }

    for (auto&& [address, nonces] : commitMap)
    {
        ledger::StorageState storageState{
            .nonce = (*nonces.rbegin() + 1).convert_to<std::string>(), .balance = "1"};
        m_ledger->setStorageState(toHex(address), std::move(storageState));
    }

    uint64_t errorCount = 0;
    for (const auto& [sender, nonce] : nonces)
    {
        const auto status =
            task::syncWait(checker.checkWeb3Nonce(sender, nonce, true /*only check ledger*/));
        if (status != TransactionStatus::None)
        {
            errorCount++;
        }
    }
    BOOST_CHECK_EQUAL(errorCount, totalSize);
}

BOOST_AUTO_TEST_CASE(testAtomicInsertMemoryNonce)
{
    // 50 concurrent threads all try to insert the same (sender, nonce) pair.
    // Exactly one must succeed; the rest must return false.
    constexpr int threadCount = 50;
    auto sender = Address::generateRandomFixedBytes().toRawString();
    std::string nonce = "42";

    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i)
    {
        threads.emplace_back([&]() {
            bool inserted = task::syncWait(checker.insertMemoryNonce(sender, nonce));
            if (inserted)
            {
                successCount.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : threads)
    {
        t.join();
    }

    BOOST_CHECK_EQUAL(successCount.load(), 1);
}

BOOST_AUTO_TEST_CASE(FIB52_PairHashDistinguishesSecondElement)
{
    // FIB-52: The old PairHash only hashed pair.first (sender) and ignored pair.second
    // (nonce), causing all (sender, *) pairs to collide in the same hash bucket. The fix
    // combines hashes of both elements using a Fibonacci multiplier to spread nonces.

    bcos::txpool::PairHash hasher;

    const std::string senderA = "sender_alpha_addr_xyz";
    const std::string senderB = "sender_beta_addr_abc";
    const std::string nonce1 = "0x0001";
    const std::string nonce2 = "0x0002";

    // Same sender, different nonces must hash differently (old code: same hash = collision)
    auto h1 = hasher(std::make_pair(senderA, nonce1));
    auto h2 = hasher(std::make_pair(senderA, nonce2));
    BOOST_CHECK_NE(h1, h2);

    // Consistency: same inputs must produce the same hash
    BOOST_CHECK_EQUAL(h1, hasher(std::make_pair(senderA, nonce1)));

    // Different senders, same nonce must also hash differently
    auto h3 = hasher(std::make_pair(senderB, nonce1));
    BOOST_CHECK_NE(h1, h3);

    // Equality predicate: (A,n1) == (A,n1) and (A,n1) != (A,n2)
    BOOST_CHECK(hasher(std::make_pair(senderA, nonce1), std::make_pair(senderA, nonce1)));
    BOOST_CHECK(!hasher(std::make_pair(senderA, nonce1), std::make_pair(senderA, nonce2)));
    BOOST_CHECK(!hasher(std::make_pair(senderA, nonce1), std::make_pair(senderB, nonce1)));
}

BOOST_AUTO_TEST_CASE(FIB57_RejectOversizedNonceString)
{
    // FIB-57: Oversized nonce strings must be rejected at the earliest validation point —
    // checkWeb3Nonce() — before any u256 conversion, to prevent LRU capacity accounting bypass.
    constexpr size_t MAX_LEN = 78;  // max decimal digits of u256

    // A nonce exactly at the limit (78 chars) should pass checkWeb3Nonce
    const std::string sender78 = Address::generateRandomFixedBytes().toRawString();
    const std::string nonce78(MAX_LEN, '1');
    auto status78 = task::syncWait(checker.checkWeb3Nonce(sender78, nonce78));
    BOOST_CHECK_EQUAL(status78, TransactionStatus::None);

    // A nonce one byte over the limit (79 chars) must be rejected by checkWeb3Nonce
    const std::string sender79 = Address::generateRandomFixedBytes().toRawString();
    const std::string nonce79(MAX_LEN + 1, '1');
    auto status79 = task::syncWait(checker.checkWeb3Nonce(sender79, nonce79));
    BOOST_CHECK_EQUAL(status79, TransactionStatus::NonceCheckFail);

    // A very long nonce (1000 chars) must also be rejected by checkWeb3Nonce
    const std::string senderLong = Address::generateRandomFixedBytes().toRawString();
    const std::string nonce1000(1000, '9');
    auto statusLong = task::syncWait(checker.checkWeb3Nonce(senderLong, nonce1000));
    BOOST_CHECK_EQUAL(statusLong, TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_CASE(FIB58_NonceNormalizationDetectsDuplicates)
{
    // FIB-58: insertMemoryNonce and checkWeb3Nonce used different nonce representations
    // (raw string vs u256), so "0x1", "0x01", and "1" were treated as distinct nonces.
    // The fix normalizes via toQuantity(u256(nonce)) in both paths so all forms map to the
    // same canonical key.
    const std::string sender1 = Address::generateRandomFixedBytes().toRawString();

    // Insert nonce "0x1" (hex notation)
    task::syncWait(checker.insertMemoryNonce(sender1, "0x1"));

    // "0x1", "0x01", and "1" should all be detected as duplicates
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender1, "0x1")), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender1, "0x01")), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender1, "1")), TransactionStatus::NonceCheckFail);

    // Different nonce should still pass
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender1, "0x2")), TransactionStatus::None);

    // Decimal 16 == hex 0x10: both should collide after inserting "0x10"
    const std::string sender2 = Address::generateRandomFixedBytes().toRawString();
    task::syncWait(checker.insertMemoryNonce(sender2, "0x10"));
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender2, "16")), TransactionStatus::NonceCheckFail);
    BOOST_CHECK_EQUAL(
        task::syncWait(checker.checkWeb3Nonce(sender2, "0x10")), TransactionStatus::NonceCheckFail);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test