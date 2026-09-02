/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-framework/mempool/AnyMemPool.h"
#include "bcos-framework/mempool/MemPool.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <proxy/v3/proxy.h>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace bcos;
using namespace bcos::mempool;
using namespace bcos::protocol;
using namespace bcos::crypto;
using namespace bcos::executor_v1;

namespace bcos::test
{

/// Create a shared_ptr<Transaction> backed by a FakeIt mock.
/// The mock must outlive all shared_ptrs created from it.
inline protocol::Transaction::Ptr makeMockTx(fakeit::Mock<protocol::Transaction>& mock)
{
    return {&mock.get(), [](auto*) {}};
}

/// Minimal mock mempool that satisfies MemPool<MockMemPool, MockStateStorage>.
/// Used to verify that AnyMemPool correctly delegates through type erasure.
struct MockMemPool
{
    std::vector<protocol::Transaction::Ptr> m_addedTransactions;
    std::vector<protocol::Transaction::Ptr> m_sealedTransactions;
    int64_t m_sealLimit{};
    bool m_sealCalled{false};
    bool m_removeByStateCalled{false};
    std::vector<bcos::crypto::HashType> m_removedHashes;
    std::vector<bcos::crypto::HashType> m_getHashes;
    bool m_getCalled{false};

    void add(std::vector<protocol::Transaction::Ptr> transactions)
    {
        m_addedTransactions.insert(
            m_addedTransactions.end(), transactions.begin(), transactions.end());
    }

    template <class StateStorage>
    void seal(int64_t limit, StateStorage& /*state*/,
        std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out)
    {
        m_sealCalled = true;
        m_sealLimit = limit;
        if (!m_addedTransactions.empty())
        {
            *out++ = m_addedTransactions.front();
            m_sealedTransactions.push_back(m_addedTransactions.front());
            m_addedTransactions.erase(m_addedTransactions.begin());
        }
    }

    template <class StateStorage>
    void remove(StateStorage& /*state*/)
    {
        m_removeByStateCalled = true;
        m_addedTransactions.clear();
    }

    void remove(std::vector<bcos::crypto::HashType> hashes)
    {
        m_removedHashes = std::move(hashes);
    }

    std::vector<protocol::Transaction::Ptr> get(std::vector<bcos::crypto::HashType> hashes)
    {
        m_getCalled = true;
        m_getHashes = std::move(hashes);
        return m_addedTransactions;
    }
};

/// Minimal state storage mock that satisfies ReadableStorage/ReadWriteStorage concepts.
struct MockStateStorage
{
    std::unordered_map<std::string, std::unordered_map<std::string, storage::Entry>> data;

    task::Task<std::optional<storage::Entry>> readOne(StateKeyView key)
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

    task::Task<void> writeOne(StateKeyView key, storage::Entry value)
    {
        auto [table, field] = key.get();
        data[std::string(table)][std::string(field)] = std::move(value);
        co_return;
    }

    task::Task<bool> existsOne(StateKeyView key)
    {
        auto value = co_await readOne(key);
        co_return value.has_value();
    }
};

/// Compile-time verification that mocks satisfy the MemPool concept.
static_assert(MemPool<MockMemPool, MockStateStorage>, "MockMemPool must satisfy MemPool concept");

/// A non-copyable, non-movable mock that mimics the constraints of the real
/// MemPoolImpl (which contains std::mutex).  Used to verify that AnyMemPool's
/// in_place_type constructor correctly stores types that are neither copyable
/// nor movable.
struct NonCopyableMemPool
{
    std::mutex m_mutex;  // makes this type non-copyable and non-movable
    std::vector<protocol::Transaction::Ptr> m_addedTransactions;
    bool m_addCalled{false};

    NonCopyableMemPool() = default;
    NonCopyableMemPool(const NonCopyableMemPool&) = delete;
    NonCopyableMemPool& operator=(const NonCopyableMemPool&) = delete;
    NonCopyableMemPool(NonCopyableMemPool&&) = delete;
    NonCopyableMemPool& operator=(NonCopyableMemPool&&) = delete;

    void add(std::vector<protocol::Transaction::Ptr> transactions)
    {
        m_addCalled = true;
        m_addedTransactions = std::move(transactions);
    }

    template <class StateStorage>
    void seal(int64_t /*limit*/, StateStorage& /*state*/,
        std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out)
    {
        for (auto& tx : m_addedTransactions)
        {
            *out++ = tx;
        }
        m_addedTransactions.clear();
    }

    template <class StateStorage>
    void remove(StateStorage& /*state*/)
    {
        m_addedTransactions.clear();
    }

    void remove(std::vector<bcos::crypto::HashType> /*hashes*/)
    {
        m_addedTransactions.clear();
    }

    std::vector<protocol::Transaction::Ptr> get(std::vector<bcos::crypto::HashType> /*hashes*/)
    {
        return m_addedTransactions;
    }
};

}  // namespace bcos::test

/// Helper to construct an AnyMemPool from a MockMemPool via pro::make_proxy.
template <class StateStorage = bcos::test::MockStateStorage>
static auto makeAny(bcos::test::MockMemPool& mock)
{
    return pro::make_proxy<AnyMemPoolFacade<StateStorage>, bcos::test::MockMemPool>(mock);
}

BOOST_AUTO_TEST_SUITE(TestAnyMemPool)

/// Verify AnyMemPool can be constructed with a mock satisfying MemPool.
BOOST_AUTO_TEST_CASE(constructWithMock)
{
    bcos::test::MockMemPool mock;
    auto any = makeAny(mock);

    BOOST_CHECK(any.has_value());
}

/// Verify add() / get() round-trip through the type-erased wrapper.
BOOST_AUTO_TEST_CASE(addAndGetRoundtrip)
{
    bcos::test::MockMemPool mock;
    auto any = makeAny(mock);

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any->add(std::vector<protocol::Transaction::Ptr>{tx});

    bcos::crypto::HashType h{0xaa};
    auto result = any->get({h});
    // The mock's get() returns whatever was added, ignoring the hash arg
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);

    // Second get() call: the mock always returns m_addedTransactions
    // (which still has the single tx), regardless of the hash argument.
    auto result2 = any->get({bcos::crypto::HashType{0xbb}});
    BOOST_CHECK_EQUAL(result2.size(), 1);
}

/// Verify seal() is correctly delegated through the type-erased wrapper.
BOOST_AUTO_TEST_CASE(sealProducesOutput)
{
    bcos::test::MockMemPool mock;
    bcos::test::MockStateStorage state;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);
    auto any = makeAny(mock);

    std::vector<protocol::Transaction::Ptr> out;
    any->seal(100, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 1);
    BOOST_CHECK(out[0] == tx);
}

/// Verify seal() drain: after sealing, transactions are removed from the pool.
BOOST_AUTO_TEST_CASE(sealDrainsTransactions)
{
    bcos::test::MockMemPool mock;
    bcos::test::MockStateStorage state;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);
    auto any = makeAny(mock);

    std::vector<protocol::Transaction::Ptr> out;
    any->seal(100, state, std::back_inserter(out));

    // After seal, get() should return empty (mock's seal drains m_addedTransactions)
    auto result = any->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK(result.empty());
}

/// Verify remove(StateStorage) clears the pool.
BOOST_AUTO_TEST_CASE(removeByStateClears)
{
    bcos::test::MockMemPool mock;
    bcos::test::MockStateStorage state;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);
    auto any = makeAny(mock);

    any->remove(state);

    // After remove, the pool should be empty
    auto result = any->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK(result.empty());
}

/// Verify remove(hashes) is correctly delegated.
BOOST_AUTO_TEST_CASE(removeByHashesDelegates)
{
    bcos::test::MockMemPool mock;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);
    auto any = makeAny(mock);

    std::vector<bcos::crypto::HashType> hashes{bcos::crypto::HashType{0x01}};
    any->remove(hashes);

    // remove sets m_removedHashes in the mock; verify via get()
    auto result = any->get({bcos::crypto::HashType{0x02}});
    BOOST_CHECK_EQUAL(result.size(), 1);
}

/// Verify move construction does not break delegation.
BOOST_AUTO_TEST_CASE(moveConstruction)
{
    bcos::test::MockMemPool mock;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);
    auto any1 = makeAny(mock);

    // Move construct
    auto any2 = std::move(any1);

    BOOST_CHECK(any2.has_value());
    BOOST_CHECK(!any1.has_value());

    // any2 should still have the transaction
    auto result = any2->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);
}

/// Verify move assignment does not break delegation.
BOOST_AUTO_TEST_CASE(moveAssignment)
{
    bcos::test::MockMemPool mock1, mock2;
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock1.m_addedTransactions.push_back(tx);
    auto any1 = makeAny(mock1);
    auto any2 = makeAny(mock2);

    any2 = std::move(any1);

    BOOST_CHECK(any2.has_value());
    BOOST_CHECK(!any1.has_value());

    auto result = any2->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);
}

/// Verify in_place_type construction with a non-copyable, non-movable type.
/// This is the critical path for wrapping real MemPoolImpl (which contains
/// std::mutex and is therefore neither copyable nor movable).
BOOST_AUTO_TEST_CASE(constructNonCopyableInPlace)
{
    // Construct AnyMemPool with a non-copyable mempool via pro::make_proxy.
    auto any = pro::make_proxy<AnyMemPoolFacade<bcos::test::MockStateStorage>,
        bcos::test::NonCopyableMemPool>();

    BOOST_CHECK(any.has_value());

    // Verify add() and get() round-trip
    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any->add(std::vector<protocol::Transaction::Ptr>{tx});

    auto result = any->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);
}

/// Verify in_place_type with extra constructor arguments.
BOOST_AUTO_TEST_CASE(constructNonCopyableInPlaceWithArgs)
{
    // NonCopyableMemPool takes no extra args, but this validates the variadic
    // forwarding path through pro::make_proxy.
    auto any = pro::make_proxy<AnyMemPoolFacade<bcos::test::MockStateStorage>,
        bcos::test::NonCopyableMemPool>();

    BOOST_CHECK(any.has_value());

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any->add(std::vector<protocol::Transaction::Ptr>{tx});

    auto result = any->get({bcos::crypto::HashType{0x02}});
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);
}

/// Verify seal() through an in_place_type-constructed AnyMemPool works.
BOOST_AUTO_TEST_CASE(sealNonCopyableInPlace)
{
    auto any = pro::make_proxy<AnyMemPoolFacade<bcos::test::MockStateStorage>,
        bcos::test::NonCopyableMemPool>();
    bcos::test::MockStateStorage state;

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any->add(std::vector<protocol::Transaction::Ptr>{tx});

    std::vector<protocol::Transaction::Ptr> out;
    any->seal(100, state, std::back_inserter(out));

    BOOST_CHECK_EQUAL(out.size(), 1);
    BOOST_CHECK(out[0] == tx);
}

/// Verify remove(StateStorage) through an in_place_type-constructed AnyMemPool.
BOOST_AUTO_TEST_CASE(removeByStateNonCopyableInPlace)
{
    auto any = pro::make_proxy<AnyMemPoolFacade<bcos::test::MockStateStorage>,
        bcos::test::NonCopyableMemPool>();
    bcos::test::MockStateStorage state;

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any->add(std::vector<protocol::Transaction::Ptr>{tx});

    any->remove(state);

    auto result = any->get({bcos::crypto::HashType{0x01}});
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_SUITE_END()
