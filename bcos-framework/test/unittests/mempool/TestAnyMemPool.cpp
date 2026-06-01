/**
 *  Copyright (C) 2025 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "bcos-framework/mempool/AnyMemPool.h"
#include "bcos-framework/mempool/MemPool.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include "bcos-utilities/Common.h"
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <iterator>
#include <optional>
#include <stdexcept>
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

    void remove(std::vector<bcos::crypto::HashType> hashes) { m_removedHashes = std::move(hashes); }

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

}  // namespace bcos::test

BOOST_AUTO_TEST_SUITE(TestAnyMemPool)

/// Verify AnyMemPool can be constructed with a mock satisfying MemPool.
BOOST_AUTO_TEST_CASE(constructWithMock)
{
    bcos::test::MockMemPool mock;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    BOOST_CHECK(static_cast<bool>(any));
}

/// Verify add() is correctly delegated through the type-erased wrapper.
BOOST_AUTO_TEST_CASE(addDelegates)
{
    bcos::test::MockMemPool mock;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    std::vector<protocol::Transaction::Ptr> txs{tx};
    any.add(txs);

    BOOST_CHECK_EQUAL(mock.m_addedTransactions.size(), 1);
    BOOST_CHECK(mock.m_addedTransactions[0] == tx);
}

/// Verify seal() is correctly delegated through the type-erased wrapper.
BOOST_AUTO_TEST_CASE(sealDelegates)
{
    bcos::test::MockMemPool mock;
    bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);

    std::vector<protocol::Transaction::Ptr> out;
    constexpr int64_t kLimit = 42;
    any.seal(kLimit, state, std::back_inserter(out));

    BOOST_CHECK(mock.m_sealCalled);
    BOOST_CHECK_EQUAL(mock.m_sealLimit, kLimit);
    BOOST_CHECK_EQUAL(out.size(), 1);
}

/// Verify remove(StateStorage) is correctly delegated.
BOOST_AUTO_TEST_CASE(removeByStateDelegates)
{
    bcos::test::MockMemPool mock;
    bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    any.remove(state);

    BOOST_CHECK(mock.m_removeByStateCalled);
}

/// Verify remove(hashes) is correctly delegated.
BOOST_AUTO_TEST_CASE(removeByHashesDelegates)
{
    bcos::test::MockMemPool mock;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    std::vector<bcos::crypto::HashType> hashes{bcos::crypto::HashType{0x01}};
    any.remove(hashes);

    BOOST_CHECK_EQUAL(mock.m_removedHashes.size(), 1);
}

/// Verify get() is correctly delegated.
BOOST_AUTO_TEST_CASE(getDelegates)
{
    bcos::test::MockMemPool mock;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any(mock);

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    mock.m_addedTransactions.push_back(tx);

    std::vector<bcos::crypto::HashType> hashes{bcos::crypto::HashType{0x02}};
    auto result = any.get(hashes);

    BOOST_CHECK(mock.m_getCalled);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result[0] == tx);
}

/// Verify move construction does not break delegation.
BOOST_AUTO_TEST_CASE(moveConstruction)
{
    bcos::test::MockMemPool mock;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any1(mock);

    // Move construct
    AnyMemPool<bcos::test::MockStateStorage> any2(std::move(any1));

    BOOST_CHECK(static_cast<bool>(any2));
    BOOST_CHECK(!static_cast<bool>(any1));

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any2.add(std::vector<protocol::Transaction::Ptr>{tx});
    BOOST_CHECK_EQUAL(mock.m_addedTransactions.size(), 1);
}

/// Verify move assignment does not break delegation.
BOOST_AUTO_TEST_CASE(moveAssignment)
{
    bcos::test::MockMemPool mock1;
    bcos::test::MockMemPool mock2;
    [[maybe_unused]] bcos::test::MockStateStorage state;
    AnyMemPool<bcos::test::MockStateStorage> any1(mock1);
    AnyMemPool<bcos::test::MockStateStorage> any2(mock2);

    static_cast<void>(any2 = std::move(any1));

    BOOST_CHECK(static_cast<bool>(any2));
    BOOST_CHECK(!static_cast<bool>(any1));

    fakeit::Mock<protocol::Transaction> mockTx;
    auto tx = bcos::test::makeMockTx(mockTx);
    any2.add(std::vector<protocol::Transaction::Ptr>{tx});
    BOOST_CHECK_EQUAL(mock1.m_addedTransactions.size(), 1);
    BOOST_CHECK_EQUAL(mock2.m_addedTransactions.size(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
