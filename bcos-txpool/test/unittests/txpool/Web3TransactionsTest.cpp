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

BOOST_AUTO_TEST_CASE(seal_multiple_senders_and_gaps)
{
    Web3Transactions pool;
    std::string a("AAAAAAAAAAAAAAAAAAAA", 20);
    std::string b("BBBBBBBBBBBBBBBBBBBB", 20);

    pool.add(std::vector{makeTx(a, 0), makeTx(a, 2), makeTx(b, 0)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(100, state, out));

    // Expect to seal A:0 and B:0 only; A:2 is blocked by gap at 1
    // Order across senders is implementation-defined; check membership and counts
    BOOST_CHECK_EQUAL(out.size(), 2);
    auto sealedNoncesBySender = std::unordered_map<std::string, std::vector<int64_t>>{};
    for (auto& t : out)
    {
        sealedNoncesBySender[std::string(t->sender())].push_back(
            std::stoll(std::string(t->nonce())));
    }
    BOOST_CHECK(sealedNoncesBySender.contains(a));
    BOOST_CHECK(sealedNoncesBySender.contains(b));
    BOOST_CHECK(sealedNoncesBySender[a].size() == 1 && sealedNoncesBySender[a][0] == 0);
    BOOST_CHECK(sealedNoncesBySender[b].size() == 1 && sealedNoncesBySender[b][0] == 0);

    // Nonce advanced to 1 for A and 1 for B
    auto nonceA = readNonce(state, a);
    auto nonceB = readNonce(state, b);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK(nonceB.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "1");
    BOOST_CHECK_EQUAL(nonceB.value(), "1");
}

BOOST_AUTO_TEST_CASE(seal_with_existing_ledger_nonce)
{
    Web3Transactions pool;
    std::string sender("CCCCCCCCCCCCCCCCCCCC", 20);

    // Ledger already has nonce = 5, so only >=5 contiguous will be sealed
    MapStateStorage state{};
    setNonce(state, sender, "5");

    pool.add(
        std::vector{makeTx(sender, 3), makeTx(sender, 4), makeTx(sender, 5), makeTx(sender, 6)});

    std::vector<protocol::Transaction::Ptr> out;
    task::syncWait(pool.seal(100, state, out));

    // Expect only 5 and 6 sealed
    BOOST_CHECK_EQUAL(out.size(), 2);
    auto ns = std::vector<int64_t>{
        std::stoll(std::string(out[0]->nonce())), std::stoll(std::string(out[1]->nonce()))};
    ::ranges::sort(ns);
    BOOST_CHECK(ns[0] == 5 && ns[1] == 6);

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
