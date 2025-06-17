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
#include <memory>
#include <range/v3/all.hpp>
#include <range/v3/view/enumerate.hpp>
#include <range/v3/view/transform.hpp>
#include <ranges>
#include <set>
#include <unordered_map>

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
        task::syncWait(checker.insertMemoryNonce(sender, nonce));
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
        task::syncWait(checker.insertMemoryNonce(sender, (*commitMap[sender].rbegin() + 2).str()));
        auto nonce = task::syncWait(checker.getPendingNonce(toHex(sender)));
        BOOST_CHECK(nonce.has_value());
        BOOST_CHECK_EQUAL(nonce.value(), *commitMap[sender].rbegin() + 2 + 1);
    }

    // new sender
    {
        auto&& newSender = Address::generateRandomFixedBytes().toRawString();
        auto nonce = task::syncWait(checker.getPendingNonce(toHex(newSender)));
        BOOST_CHECK(!nonce.has_value());

        task::syncWait(checker.insertMemoryNonce(newSender, "1"));
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
            task::syncWait(checker.insertMemoryNonce(sender, nonce));
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test