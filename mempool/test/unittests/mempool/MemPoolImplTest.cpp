/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/mempool/MemPool.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <iterator>
#include <range/v3/all.hpp>
#include <unordered_map>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::executor_v1;

namespace bcos::test
{
struct MapStateStorage
{
    using Value = storage::Entry;
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> data;
    task::Task<std::optional<Value>> readOne(StateKeyView key)
    {
        auto [table, field] = key.get();
        if (auto tIt = data.find(std::string(table)); tIt != data.end())
        {
            if (auto fIt = tIt->second.find(std::string(field)); fIt != tIt->second.end())
            {
                co_return std::make_optional(fIt->second);
            }
        }
        co_return std::nullopt;
    }

    task::Task<std::optional<Value>> readOne(StateKey key)
    {
        co_return co_await readOne(StateKeyView{key});
    }

    template <class Keys>
    task::Task<std::vector<std::optional<Value>>> readSome(Keys keys)
    {
        std::vector<std::optional<Value>> results;
        if constexpr (::ranges::sized_range<Keys>)
        {
            results.reserve(::ranges::size(keys));
        }
        else
        {
            results.reserve(::ranges::distance(keys));
        }

        for (auto&& key : keys)
        {
            results.emplace_back(co_await readOne(StateKeyView{key}));
        }
        co_return results;
    }

    task::Task<void> writeOne(StateKey key, Value value)
    {
        StateKeyView view{key};
        auto [table, field] = view.get();
        data[std::string(table)][std::string(field)] = std::move(value);
        co_return;
    }

    task::Task<void> writeOne(StateKeyView key, Value value)
    {
        auto [table, field] = key.get();
        data[std::string(table)][std::string(field)] = std::move(value);
        co_return;
    }

    template <class KVs>
    task::Task<void> writeSome(KVs keyValues)
    {
        for (auto&& kv : keyValues)
        {
            StateKey key{std::get<0>(kv)};
            auto& value = std::get<1>(kv);
            StateKeyView view{key};
            auto [table, field] = view.get();
            data[std::string(table)][std::string(field)] = value;
        }
        co_return;
    }

    task::Task<bool> existsOne(StateKeyView key)
    {
        auto value = co_await readOne(key);
        co_return value.has_value();
    }

    task::Task<bool> existsOne(StateKey key) { co_return co_await existsOne(StateKeyView{key}); }
};

static_assert(mempool::MemPool<MemPoolImpl, MapStateStorage>);

static bytes toBytes(std::string_view s)
{
    return {reinterpret_cast<const byte*>(s.data()),
        reinterpret_cast<const byte*>(s.data()) + s.size()};
}

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

static protocol::Transaction::Ptr makeTx(std::string_view senderBytes, int64_t nonce)
{
    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().data.to.assign(senderBytes.begin(), senderBytes.end());
    tx->setNonce(std::to_string(nonce));
    tx->forceSender(toBytes(senderBytes));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    tx->markClean();
    tx->setImportTime(nonce);
    return tx;
}

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

BOOST_AUTO_TEST_SUITE(MemPoolImplTest)

BOOST_AUTO_TEST_CASE(seal_single_sender_contiguous)
{
    MemPoolImpl pool;
    std::string sender("aaaaaaaaaaaaaaaaaaaa", 20);
    std::vector<protocol::Transaction::Ptr> txs;
    txs.emplace_back(makeTx(sender, 0));
    txs.emplace_back(makeTx(sender, 1));
    txs.emplace_back(makeTx(sender, 2));
    pool.add(txs);

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(100, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 3);
    auto nonce = readNonce(state, sender);
    BOOST_CHECK(nonce.has_value());
    BOOST_CHECK_EQUAL(nonce.value(), "3");
}

BOOST_AUTO_TEST_CASE(seal_limit_within_single_sender)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 2;
    std::string sender("LLLLLLLLLLLLLLLLLLLL", kSenderBytes);

    std::vector<protocol::Transaction::Ptr> txsA{
        makeTx(sender, 0), makeTx(sender, 1), makeTx(sender, 2)};
    pool.add(txsA);
    std::string otherSender("MMMMMMMMMMMMMMMMMMMM", kSenderBytes);
    pool.add(std::vector{makeTx(otherSender, 0)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(kSealLimit, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonces = ::ranges::views::transform(out, [](auto& txPtr) {
        return std::stoll(std::string(txPtr->nonce()));
    }) | ::ranges::to<std::vector>();
    ::ranges::sort(nonces);
    BOOST_CHECK(nonces[0] == 0 && nonces[1] == 1);

    auto nonceA = readNonce(state, sender);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "2");
    auto nonceB = readNonce(state, otherSender);
    BOOST_CHECK(!nonceB.has_value());
}

BOOST_AUTO_TEST_CASE(seal_limit_across_senders)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    constexpr int kSealLimit = 2;
    std::string senderA("NNNNNNNNNNNNNNNNNNNN", kSenderBytes);
    std::string senderB("OOOOOOOOOOOOOOOOOOOO", kSenderBytes);

    pool.add(std::vector{makeTx(senderA, 0), makeTx(senderA, 2), makeTx(senderB, 0)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(kSealLimit, state, std::back_inserter(out));

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
    BOOST_CHECK(foundA0);
    BOOST_CHECK(foundB0);

    auto nonceA = readNonce(state, senderA);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "1");
    auto nonceB = readNonce(state, senderB);
    BOOST_CHECK(nonceB.has_value());
    BOOST_CHECK_EQUAL(nonceB.value(), "1");
}

BOOST_AUTO_TEST_CASE(seal_limit_exact_boundary)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string sender("PPPPPPPPPPPPPPPPPPPP", kSenderBytes);
    pool.add(std::vector{makeTx(sender, 0), makeTx(sender, 1)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(2, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonce = readNonce(state, sender);
    BOOST_CHECK(nonce.has_value());
    BOOST_CHECK_EQUAL(nonce.value(), "2");
}

BOOST_AUTO_TEST_CASE(seal_multiple_senders_and_gaps)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string senderA("QQQQQQQQQQQQQQQQQQQQ", kSenderBytes);
    std::string senderB("RRRRRRRRRRRRRRRRRRRR", kSenderBytes);

    pool.add(std::vector{makeTx(senderA, 0), makeTx(senderA, 2), makeTx(senderA, 3),
        makeTx(senderB, 1), makeTx(senderB, 2)});

    MapStateStorage state{};
    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(10, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 1);
    BOOST_CHECK_EQUAL(std::string(out.front()->sender()), senderA);
    BOOST_CHECK_EQUAL(std::string(out.front()->nonce()), "0");

    auto nonceA = readNonce(state, senderA);
    BOOST_CHECK(nonceA.has_value());
    BOOST_CHECK_EQUAL(nonceA.value(), "1");
    auto nonceB = readNonce(state, senderB);
    BOOST_CHECK(!nonceB.has_value());
}

BOOST_AUTO_TEST_CASE(seal_with_existing_ledger_nonce)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string sender("SSSSSSSSSSSSSSSSSSSS", kSenderBytes);
    pool.add(std::vector{makeTx(sender, 0), makeTx(sender, 1), makeTx(sender, 2)});

    MapStateStorage state{};
    setNonce(state, sender, "1");

    std::vector<protocol::Transaction::Ptr> out;
    pool.seal(10, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 2);
    auto nonces = ::ranges::views::transform(out, [](auto& txPtr) {
        return std::stoll(std::string(txPtr->nonce()));
    }) | ::ranges::to<std::vector>();
    ::ranges::sort(nonces);
    BOOST_CHECK(nonces == std::vector<long long>({1, 2}));

    auto nonce = readNonce(state, sender);
    BOOST_CHECK(nonce.has_value());
    BOOST_CHECK_EQUAL(nonce.value(), "3");
}

BOOST_AUTO_TEST_CASE(remove_by_state_drops_confirmed)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string senderA("TTTTTTTTTTTTTTTTTTTT", kSenderBytes);
    std::string senderB("UUUUUUUUUUUUUUUUUUUU", kSenderBytes);
    auto a0 = makeTx(senderA, 0);
    auto a1 = makeTx(senderA, 1);
    auto a2 = makeTx(senderA, 2);
    auto b0 = makeTx(senderB, 0);
    pool.add(std::vector{a0, a1, a2, b0});

    MapStateStorage state{};
    setNonce(state, senderA, "2");

    pool.remove(state);

    auto fetched = pool.get(std::vector{a0->hash(), a1->hash(), a2->hash(), b0->hash()});
    BOOST_CHECK(!fetched[0]);
    BOOST_CHECK(!fetched[1]);
    BOOST_CHECK(fetched[2]);
    BOOST_CHECK(fetched[3]);
}

BOOST_AUTO_TEST_CASE(remove_by_hashes_respects_per_sender_max)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string senderA("VVVVVVVVVVVVVVVVVVVV", kSenderBytes);
    std::string senderB("WWWWWWWWWWWWWWWWWWWW", kSenderBytes);
    auto a0 = makeTx(senderA, 0);
    auto a1 = makeTx(senderA, 1);
    auto a2 = makeTx(senderA, 2);
    auto b0 = makeTx(senderB, 0);
    auto b1 = makeTx(senderB, 1);
    pool.add(std::vector{a0, a1, a2, b0, b1});

    pool.remove(std::vector{a1->hash(), b0->hash()});

    auto fetched =
        pool.get(std::vector{a0->hash(), a1->hash(), a2->hash(), b0->hash(), b1->hash()});
    BOOST_CHECK(!fetched[0]);
    BOOST_CHECK(!fetched[1]);
    BOOST_CHECK(fetched[2]);
    BOOST_CHECK(!fetched[3]);
    BOOST_CHECK(fetched[4]);
}

BOOST_AUTO_TEST_CASE(get_returns_in_order_with_null_for_missing)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string sender("XXXXXXXXXXXXXXXXXXXX", kSenderBytes);
    auto tx0 = makeTx(sender, 0);
    auto tx1 = makeTx(sender, 1);
    pool.add(std::vector{tx0, tx1});

    auto missingHash = h256("1234567890123456789012345678901234567890123456789012345678901234");
    auto fetched = pool.get(std::vector{tx1->hash(), missingHash, tx0->hash()});

    BOOST_CHECK_EQUAL(fetched.size(), 3);
    BOOST_REQUIRE(fetched[0]);
    BOOST_CHECK_EQUAL(fetched[0]->hash(), tx1->hash());
    BOOST_CHECK(!fetched[1]);
    BOOST_REQUIRE(fetched[2]);
    BOOST_CHECK_EQUAL(fetched[2]->hash(), tx0->hash());
}

BOOST_AUTO_TEST_CASE(get_duplicate_hashes_returns_duplicates)
{
    MemPoolImpl pool;
    constexpr int kSenderBytes = 20;
    std::string sender("YYYYYYYYYYYYYYYYYYYY", kSenderBytes);
    auto tx0 = makeTx(sender, 0);
    pool.add(std::vector{tx0});

    auto fetched = pool.get(std::vector{tx0->hash(), tx0->hash()});

    BOOST_CHECK_EQUAL(fetched.size(), 2);
    BOOST_REQUIRE(fetched[0]);
    BOOST_REQUIRE(fetched[1]);
    BOOST_CHECK_EQUAL(fetched[0]->hash(), tx0->hash());
    BOOST_CHECK_EQUAL(fetched[1]->hash(), tx0->hash());
}

BOOST_AUTO_TEST_CASE(get_empty_input_returns_empty)
{
    MemPoolImpl pool;
    auto fetched = pool.get(std::vector<bcos::crypto::HashType>{});
    BOOST_CHECK(fetched.empty());
}

BOOST_AUTO_TEST_CASE(testAddNullTransaction)
{
    MemPoolImpl pool;
    pool.add(std::vector<protocol::Transaction::Ptr>{nullptr});
    auto fetched = pool.get(std::vector<bcos::crypto::HashType>{});
    BOOST_CHECK(fetched.empty());
}

BOOST_AUTO_TEST_CASE(testAddEmptyHashTransaction)
{
    MemPoolImpl pool;
    auto tx = std::make_shared<TestTransactionImpl>();
    tx->setNonce("0");
    tx->markClean();
    pool.add(std::vector<protocol::Transaction::Ptr>{tx});
    auto fetched = pool.get(std::vector<bcos::crypto::HashType>{});
    BOOST_CHECK(fetched.empty());
}

BOOST_AUTO_TEST_CASE(testAddInvalidNonceTransaction)
{
    MemPoolImpl pool;
    auto tx = makeTx(std::string(20, 'Z'), 0);
    tx->setNonce("invalid");
    pool.add(std::vector{tx});
    auto fetched = pool.get(std::vector{tx->hash()});
    BOOST_CHECK_EQUAL(fetched.size(), 1);
    BOOST_CHECK(!fetched.front());
}

BOOST_AUTO_TEST_CASE(testAddTaintedTransaction)
{
    MemPoolImpl pool;
    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().data.to.assign(20, 'a');
    tx->setNonce("0");
    tx->forceSender(bytes(20, byte{'a'}));
    Keccak256 hasher;
    tx->calculateHash(hasher);
    BOOST_CHECK_THROW(
        pool.add(std::vector<protocol::Transaction::Ptr>{tx}), InvalidTaintedTransaction);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test