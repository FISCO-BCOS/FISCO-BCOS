/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-txpool/txpool/storage/Web3Transactions.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <range/v3/all.hpp>
#include <unordered_map>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::executor_v1;

namespace bcos::test
{
// A tiny in-memory storage implementing storage2 tag_invoke for StateKeyView/StateValue.
struct MapStateStorage
{
    using Value = storage::Entry;
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> data;
};

// ReadOne
inline task::Task<std::optional<MapStateStorage::Value>> tag_invoke(
    bcos::storage2::tag_t<bcos::storage2::readOne>, MapStateStorage& storage,
    const StateKeyView key)
{
    auto [table, field] = key.get();
    if (auto tIt = storage.data.find(std::string(table)); tIt != storage.data.end())
    {
        if (auto fIt = tIt->second.find(std::string(field)); fIt != tIt->second.end())
        {
            co_return std::make_optional(fIt->second);
        }
    }
    co_return std::nullopt;
}

// WriteOne
inline task::Task<void> tag_invoke(bcos::storage2::tag_t<bcos::storage2::writeOne>,
    MapStateStorage& storage, StateKey key, MapStateStorage::Value value)
{
    StateKeyView view{key};
    auto [table, field] = view.get();
    storage.data[std::string(table)][std::string(field)] = std::move(value);
    co_return;
}

// ReadSome: return vector<optional<Entry>> in the same order
template <class Keys>
inline task::Task<std::vector<std::optional<MapStateStorage::Value>>> tag_invoke(
    bcos::storage2::tag_t<bcos::storage2::readSome>, MapStateStorage& storage, Keys keys)
{
    std::vector<std::optional<MapStateStorage::Value>> results;
    results.reserve(::ranges::distance(keys));
    for (auto&& k : keys)
    {
        StateKeyView key{k};
        auto [table, field] = key.get();
        if (auto tIt = storage.data.find(std::string(table)); tIt != storage.data.end())
        {
            if (auto fIt = tIt->second.find(std::string(field)); fIt != tIt->second.end())
            {
                results.emplace_back(fIt->second);
                continue;
            }
        }
        results.emplace_back(std::nullopt);
    }
    co_return results;
}

// WriteSome: accept range of pair(StateKey, Entry)
template <class KVs>
inline task::Task<void> tag_invoke(
    bcos::storage2::tag_t<bcos::storage2::writeSome>, MapStateStorage& storage, KVs keyValues)
{
    for (auto&& kv : keyValues)
    {
        StateKey key{std::get<0>(kv)};
        auto& value = std::get<1>(kv);
        StateKeyView view{key};
        auto [table, field] = view.get();
        storage.data[std::string(table)][std::string(field)] = value;
    }
    co_return;
}

static bytes toBytes(std::string_view s)
{
    return bytes(reinterpret_cast<const byte*>(s.data()),
        reinterpret_cast<const byte*>(s.data()) + s.size());
}

static protocol::Transaction::Ptr makeTx(std::string_view senderBytes, int64_t nonce)
{
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(toBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    // give it a stable importTime ordering same as nonce
    tx->setImportTime(nonce);
    return tx;
}

// Helper to read nonce from storage via EVMAccount to validate seal() side effects
static std::optional<std::string> readNonce(MapStateStorage& s, std::string_view sender)
{
    ledger::account::EVMAccount acc{s, sender, false};
    return task::syncWait(acc.nonce());
}

static void setNonce(MapStateStorage& s, std::string_view sender, std::string nonce)
{
    ledger::account::EVMAccount acc{s, sender, false};
    task::syncWait(acc.setNonce(std::move(nonce)));
}

BOOST_AUTO_TEST_SUITE(Web3TransactionsTest)

BOOST_AUTO_TEST_CASE(seal_single_sender_contiguous)
{
    Web3Transactions pool;
    // 20-byte sender
    std::string sender("aaaaaaaaaaaaaaaaaaaa", 20);
    std::vector<protocol::Transaction::Ptr> txs;
    txs.emplace_back(makeTx(sender, 0));
    txs.emplace_back(makeTx(sender, 1));
    txs.emplace_back(makeTx(sender, 2));
    pool.add(txs);

    MapStateStorage state{};  // empty state implies starting nonce = 0
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(100, state, out));

    BOOST_CHECK_EQUAL(out.size(), 3);
    auto nonce = readNonce(state, sender);
    BOOST_CHECK(nonce.has_value());
    BOOST_CHECK_EQUAL(nonce.value(), "3");
}

BOOST_AUTO_TEST_CASE(seal_limit_within_single_sender)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 2;
    std::string sender("LLLLLLLLLLLLLLLLLLLL", kSenderBytes);

    // Add A: 0,1,2 and B: 0
    std::vector<protocol::Transaction::Ptr> txsA{
        makeTx(sender, 0), makeTx(sender, 1), makeTx(sender, 2)};
    pool.add(txsA);
    std::string otherSender("MMMMMMMMMMMMMMMMMMMM", kSenderBytes);
    pool.add(std::vector{makeTx(otherSender, 0)});

    MapStateStorage state{};  // empty state -> start nonces at 0
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    // Expect only first two from the first sender
    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonces = ::ranges::views::transform(out, [](auto& txPtr) {
        return std::stoll(std::string(txPtr->nonce()));
    }) | ::ranges::to<std::vector>();
    ::ranges::sort(nonces);
    BOOST_CHECK(nonces[0] == 0 && nonces[1] == 1);

    // A nonce advanced to 2; other sender untouched (no nonce key)
    auto nonceA = readNonce(state, sender);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "2");
    auto nonceB = readNonce(state, otherSender);
    BOOST_CHECK(!nonceB.has_value());
}

BOOST_AUTO_TEST_CASE(seal_limit_across_senders)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 2;
    std::string senderA("NNNNNNNNNNNNNNNNNNNN", kSenderBytes);
    std::string senderB("OOOOOOOOOOOOOOOOOOOO", kSenderBytes);

    // A: 0 and 2 (gap at 1), B: 0
    pool.add(std::vector{makeTx(senderA, 0), makeTx(senderA, 2), makeTx(senderB, 0)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    // Expect A:0 and B:0
    BOOST_CHECK_EQUAL(out.size(), 2);
    bool foundA0 = false;
    bool foundB0 = false;
    for (auto& txPtr : out)
    {
        auto senderStr = std::string(txPtr->sender());
        auto nonceVal = std::stoll(std::string(txPtr->nonce()));
        if (senderStr == senderA && nonceVal == 0)
        {
            foundA0 = true;
        }
        if (senderStr == senderB && nonceVal == 0)
        {
            foundB0 = true;
        }
    }
    BOOST_CHECK(foundA0 && foundB0);

    // Nonces advanced accordingly
    auto nonceA = readNonce(state, senderA);
    auto nonceB = readNonce(state, senderB);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK(nonceB.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "1");
    BOOST_CHECK_EQUAL(nonceB.value(), "1");
}

BOOST_AUTO_TEST_CASE(seal_limit_exact_boundary)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 2;
    std::string sender("PPPPPPPPPPPPPPPPPPPP", kSenderBytes);

    // A: 0,1,2 but limit=2 should only output 0,1
    pool.add(std::vector{makeTx(sender, 0), makeTx(sender, 1), makeTx(sender, 2)});
    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonces = ::ranges::views::transform(out, [](auto& txPtr) {
        return std::stoll(std::string(txPtr->nonce()));
    }) | ::ranges::to<std::vector>();
    ::ranges::sort(nonces);
    BOOST_CHECK(nonces[0] == 0 && nonces[1] == 1);

    // Nonce updated to 2
    auto nonceA = readNonce(state, sender);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "2");
}

BOOST_AUTO_TEST_CASE(seal_multiple_senders_and_gaps)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 100;
    std::string senderAlpha("AAAAAAAAAAAAAAAAAAAA", kSenderBytes);
    std::string senderBeta("BBBBBBBBBBBBBBBBBBBB", kSenderBytes);

    pool.add(std::vector{makeTx(senderAlpha, 0), makeTx(senderAlpha, 2), makeTx(senderBeta, 0)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    // Expect to seal A:0 and B:0 only; A:2 is blocked by gap at 1
    // Order across senders is implementation-defined; check membership and counts
    BOOST_CHECK_EQUAL(out.size(), 2);
    auto sealedNoncesBySender = std::unordered_map<std::string, std::vector<int64_t>>{};
    for (auto& txPtr : out)
    {
        sealedNoncesBySender[std::string(txPtr->sender())].push_back(
            std::stoll(std::string(txPtr->nonce())));
    }
    BOOST_CHECK(sealedNoncesBySender.contains(senderAlpha));
    BOOST_CHECK(sealedNoncesBySender.contains(senderBeta));
    BOOST_CHECK(
        sealedNoncesBySender[senderAlpha].size() == 1 && sealedNoncesBySender[senderAlpha][0] == 0);
    BOOST_CHECK(
        sealedNoncesBySender[senderBeta].size() == 1 && sealedNoncesBySender[senderBeta][0] == 0);

    // Nonce advanced to 1 for A and 1 for B
    auto nonceA = readNonce(state, senderAlpha);
    auto nonceB = readNonce(state, senderBeta);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK(nonceB.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "1");
    BOOST_CHECK_EQUAL(nonceB.value(), "1");
}

BOOST_AUTO_TEST_CASE(seal_with_existing_ledger_nonce)
{
    Web3Transactions pool;
    constexpr int kSenderBytes2 = 20;
    constexpr int kSealLimit2 = 100;
    std::string sender("CCCCCCCCCCCCCCCCCCCC", kSenderBytes2);

    // Ledger already has nonce = 5, so only >=5 contiguous will be sealed
    MapStateStorage state{};
    setNonce(state, sender, "5");

    constexpr int64_t kNonce3 = 3;
    constexpr int64_t kNonce4 = 4;
    constexpr int64_t kNonce5 = 5;
    constexpr int64_t kNonce6 = 6;
    pool.add(std::vector{makeTx(sender, kNonce3), makeTx(sender, kNonce4), makeTx(sender, kNonce5),
        makeTx(sender, kNonce6)});

    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit2, state, out));

    // Expect only 5 and 6 sealed
    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonceVec = std::vector<int64_t>{
        std::stoll(std::string(out[0]->nonce())), std::stoll(std::string(out[1]->nonce()))};
    ::ranges::sort(nonceVec);
    BOOST_CHECK(nonceVec[0] == 5 && nonceVec[1] == 6);

    auto nonce = readNonce(state, sender);
    BOOST_CHECK(nonce.has_value());
    BOOST_CHECK_EQUAL(nonce.value(), "7");
}

BOOST_AUTO_TEST_CASE(remove_by_state_drops_confirmed)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 100;
    std::string sender("DDDDDDDDDDDDDDDDDDDD", kSenderBytes);

    // Add 0..5
    std::vector<protocol::Transaction::Ptr> txs;
    constexpr int kMaxNonce = 5;
    for (int i = 0; i <= kMaxNonce; ++i)
    {
        txs.emplace_back(makeTx(sender, i));
    }
    pool.add(txs);

    MapStateStorage state{};
    // Ledger reports nonce = 3 (i.e., 0..3 already confirmed)
    setNonce(state, sender, "3");
    task::syncWait(pool.remove(state));

    // Now set ledger to 4 and seal, should only see 4 and 5
    setNonce(state, sender, "4");
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    BOOST_CHECK_EQUAL(out.size(), 2);
    auto firstNonce = std::stoll(std::string(out[0]->nonce()));
    auto secondNonce = std::stoll(std::string(out[1]->nonce()));
    auto nonceArray = std::array<long, 2>{firstNonce, secondNonce};
    ::ranges::sort(nonceArray);
    BOOST_CHECK(nonceArray[0] == 4 && nonceArray[1] == 5);
}

BOOST_AUTO_TEST_CASE(remove_by_hashes_respects_per_sender_max)
{
    Web3Transactions pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 100;
    std::string senderAName("EEEEEEEEEEEEEEEEEEEE", kSenderBytes);
    std::string senderBName("FFFFFFFFFFFFFFFFFFFF", kSenderBytes);

    // A: 0..5, B: 0..2
    std::vector<protocol::Transaction::Ptr> txsA;
    std::vector<protocol::Transaction::Ptr> txsB;
    constexpr int kMaxANonce = 5;
    constexpr int kMaxBNonce = 2;
    for (int i = 0; i <= kMaxANonce; ++i)
    {
        txsA.emplace_back(makeTx(senderAName, i));
    }
    for (int i = 0; i <= kMaxBNonce; ++i)
    {
        txsB.emplace_back(makeTx(senderBName, i));
    }
    pool.add(txsA);
    pool.add(txsB);

    // Choose hashes: A {1,3} -> max=3, B {0,1} -> max=1
    std::vector<crypto::HashType> toRemove{
        txsA[1]->hash(), txsA[3]->hash(), txsB[0]->hash(), txsB[1]->hash()};
    crypto::HashListView view{toRemove};
    pool.remove(view);

    // After removal, remaining for A: 0,2,4,5; for B: 2
    // From ledger points matching first remaining contiguous, we expect to seal A:4,5 and B:2
    MapStateStorage state{};
    setNonce(state, senderAName, "4");
    setNonce(state, senderBName, "2");

    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(kSealLimit, state, out));

    // Expect three txs: A:4,5 and B:2
    BOOST_CHECK_EQUAL(out.size(), 3);
    auto got = ::ranges::views::transform(out, [](auto& txPtr) {
        return std::make_pair(
            std::string(txPtr->sender()), std::stoll(std::string(txPtr->nonce())));
    }) | ::ranges::to<std::vector>();
    // Count per sender
    int aCount = 0;
    int bCount = 0;
    bool aHas4 = false;
    bool aHas5 = false;
    bool bHas2 = false;
    constexpr int kNonce4 = 4;
    constexpr int kNonce5 = 5;
    constexpr int kNonce2 = 2;
    for (auto& [senderStr, nonceValue] : got)
    {
        if (senderStr == senderAName)
        {
            ++aCount;
            if (nonceValue == kNonce4)
            {
                aHas4 = true;
            }
            if (nonceValue == kNonce5)
            {
                aHas5 = true;
            }
        }
        if (senderStr == senderBName)
        {
            ++bCount;
            if (nonceValue == kNonce2)
            {
                bHas2 = true;
            }
        }
    }
    BOOST_CHECK_EQUAL(aCount, 2);
    BOOST_CHECK_EQUAL(bCount, 1);
    BOOST_CHECK(aHas4 && aHas5 && bHas2);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
