#include "bcos-storage/bcos-storage/RocksDBStorage2.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MerkleStorage.h"
#include "bcos-task/Wait.h"
#include <boost/test/unit_test.hpp>
#include <range/v3/algorithm/find.hpp>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::storage2::memory_storage;

namespace
{
using BackendStorage = MemoryStorage<executor_v1::StateKey, storage::Entry, ORDERED>;
using LogicalBackendStorage =
    MemoryStorage<executor_v1::StateKey, storage::Entry, Attribute(ORDERED | LOGICAL_DELETION)>;
using NodeStorage = MemoryStorage<MPTNodeHash, storage::Entry, ORDERED>;

storage::Entry makeEntry(std::string_view value)
{
    storage::Entry entry;
    entry.set(std::string(value));
    return entry;
}

std::string fixedBytes(size_t size, char fill, char tail)
{
    std::string value(size, fill);
    value.back() = tail;
    return value;
}

std::string stateKeyId(const executor_v1::StateKey& key) { return key.m_tableAndKey; }

MPTNodeHash emptyRoot() { return {}; }

task::Task<MPTNodeHash> bootstrapRootWithSlot(
    MPT<NodeStorage>& trie, std::string_view address, std::string_view slotKey, std::string_view value)
{
    auto slotRoot = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey), makeEntry(value));
    if (!slotRoot)
    {
        throw std::runtime_error("bootstrapRootWithSlot: failed to create slot root");
    }
    auto root = co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
    if (!root)
    {
        throw std::runtime_error("bootstrapRootWithSlot: failed to create account root");
    }
    co_return *root;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TestMerkleStorage)

BOOST_AUTO_TEST_CASE(writeReadAccountField)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto balanceKey =
            executor_v1::StateKey("/apps/hello", ledger::ACCOUNT_TABLE_FIELDS::BALANCE);

        auto root1 = co_await storage.writeOne(balanceKey, makeEntry("100"), emptyRoot());
        BOOST_REQUIRE(root1);

        auto trieValue = co_await storage.readOne(balanceKey, *root1);
        BOOST_REQUIRE(trieValue);
        BOOST_CHECK_EQUAL(trieValue->get(), "100");

        auto backendValue = co_await storage.readOne(balanceKey);
        BOOST_REQUIRE(backendValue);
        BOOST_CHECK_EQUAL(backendValue->get(), "100");

        auto root2 = co_await storage.writeOne(balanceKey, makeEntry("200"), *root1);
        BOOST_REQUIRE(root2);

        auto oldValue = co_await storage.readOne(balanceKey, *root1);
        BOOST_REQUIRE(oldValue);
        BOOST_CHECK_EQUAL(oldValue->get(), "100");

        auto newValue = co_await storage.readOne(balanceKey, *root2);
        BOOST_REQUIRE(newValue);
        BOOST_CHECK_EQUAL(newValue->get(), "200");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadMultipleAccountFields)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto balanceKey =
            executor_v1::StateKey("/apps/account", ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto nonceKey =
            executor_v1::StateKey("/apps/account", ledger::ACCOUNT_TABLE_FIELDS::NONCE);

        auto root1 = co_await storage.writeOne(balanceKey, makeEntry("100"), emptyRoot());
        BOOST_REQUIRE(root1);

        auto root2 = co_await storage.writeOne(nonceKey, makeEntry("1"), *root1);
        BOOST_REQUIRE(root2);

        auto balanceValue = co_await storage.readOne(balanceKey, *root2);
        BOOST_REQUIRE(balanceValue);
        BOOST_CHECK_EQUAL(balanceValue->get(), "100");

        auto nonceValue = co_await storage.readOne(nonceKey, *root2);
        BOOST_REQUIRE(nonceValue);
        BOOST_CHECK_EQUAL(nonceValue->get(), "1");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadSlotValue)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);

        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        std::string table = "/apps/slot";
        std::string slotKey = "0123456789abcdef0123456789abcdef";

        auto slotRoot = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey), makeEntry("7"));
        BOOST_REQUIRE(slotRoot);

        auto accountRoot =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(table), *slotRoot);
        BOOST_REQUIRE(accountRoot);

        auto stateKey = executor_v1::StateKey(table, slotKey);
        auto slotValue = co_await storage.readOne(stateKey, *accountRoot);
        BOOST_REQUIRE(slotValue);
        BOOST_CHECK_EQUAL(slotValue->get(), "7");

        auto updatedRoot = co_await storage.writeOne(stateKey, makeEntry("9"), *accountRoot);
        BOOST_REQUIRE(updatedRoot);

        auto updatedSlotValue = co_await storage.readOne(stateKey, *updatedRoot);
        BOOST_REQUIRE(updatedSlotValue);
        BOOST_CHECK_EQUAL(updatedSlotValue->get(), "9");

        auto backendValue = co_await storage.readOne(stateKey);
        BOOST_REQUIRE(backendValue);
        BOOST_CHECK_EQUAL(backendValue->get(), "9");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadComplexFixedAddressAndSlotKeys)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address1 = fixedBytes(20, 'a', '1');
        auto address2 = fixedBytes(20, 'a', '2');
        auto slotKey1 = fixedBytes(32, 'k', '1');
        auto slotKey2 = fixedBytes(32, 'k', '2');
        auto slotKey3 = fixedBytes(32, 'k', '3');

        BOOST_REQUIRE_EQUAL(address1.size(), 20);
        BOOST_REQUIRE_EQUAL(address2.size(), 20);
        BOOST_REQUIRE_EQUAL(slotKey1.size(), 32);
        BOOST_REQUIRE_EQUAL(slotKey2.size(), 32);
        BOOST_REQUIRE_EQUAL(slotKey3.size(), 32);

        auto slotRoot1 = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey1), makeEntry("A1"));
        BOOST_REQUIRE(slotRoot1);
        auto slotRoot2 = co_await trie.writeSlot(*slotRoot1, bcos::toHex(slotKey2), makeEntry("A2"));
        BOOST_REQUIRE(slotRoot2);
        auto root1 = co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address1), *slotRoot2);
        BOOST_REQUIRE(root1);

        auto slotRoot3 = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey3), makeEntry("B3"));
        BOOST_REQUIRE(slotRoot3);
        auto root2 = co_await trie.writeStorageRoot(*root1, bcos::toHex(address2), *slotRoot3);
        BOOST_REQUIRE(root2);

        auto address1Slot1 = executor_v1::StateKey(address1, slotKey1);
        auto address1Slot2 = executor_v1::StateKey(address1, slotKey2);
        auto address2Slot3 = executor_v1::StateKey(address2, slotKey3);

        auto valueA1Root2 = co_await storage.readOne(address1Slot1, *root2);
        BOOST_REQUIRE(valueA1Root2);
        BOOST_CHECK_EQUAL(valueA1Root2->get(), "A1");

        auto valueA2Root2 = co_await storage.readOne(address1Slot2, *root2);
        BOOST_REQUIRE(valueA2Root2);
        BOOST_CHECK_EQUAL(valueA2Root2->get(), "A2");

        auto valueB3Root2 = co_await storage.readOne(address2Slot3, *root2);
        BOOST_REQUIRE(valueB3Root2);
        BOOST_CHECK_EQUAL(valueB3Root2->get(), "B3");

        auto root3 = co_await storage.writeOne(address1Slot1, makeEntry("A1-v2"), *root2);
        BOOST_REQUIRE(root3);

        auto oldValueA1 = co_await storage.readOne(address1Slot1, *root2);
        BOOST_REQUIRE(oldValueA1);
        BOOST_CHECK_EQUAL(oldValueA1->get(), "A1");

        auto newValueA1 = co_await storage.readOne(address1Slot1, *root3);
        BOOST_REQUIRE(newValueA1);
        BOOST_CHECK_EQUAL(newValueA1->get(), "A1-v2");

        auto unchangedA2 = co_await storage.readOne(address1Slot2, *root3);
        BOOST_REQUIRE(unchangedA2);
        BOOST_CHECK_EQUAL(unchangedA2->get(), "A2");

        auto unchangedB3 = co_await storage.readOne(address2Slot3, *root3);
        BOOST_REQUIRE(unchangedB3);
        BOOST_CHECK_EQUAL(unchangedB3->get(), "B3");

        auto root4 = co_await storage.writeOne(address2Slot3, makeEntry("B3-v2"), *root3);
        BOOST_REQUIRE(root4);

        auto valueA1Root4 = co_await storage.readOne(address1Slot1, *root4);
        BOOST_REQUIRE(valueA1Root4);
        BOOST_CHECK_EQUAL(valueA1Root4->get(), "A1-v2");

        auto valueB3Root4 = co_await storage.readOne(address2Slot3, *root4);
        BOOST_REQUIRE(valueB3Root4);
        BOOST_CHECK_EQUAL(valueB3Root4->get(), "B3-v2");

        auto backendA1 = co_await storage.readOne(address1Slot1);
        BOOST_REQUIRE(backendA1);
        BOOST_CHECK_EQUAL(backendA1->get(), "A1-v2");

        auto backendB3 = co_await storage.readOne(address2Slot3);
        BOOST_REQUIRE(backendB3);
        BOOST_CHECK_EQUAL(backendB3->get(), "B3-v2");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadBranchingRootsFromSameBase)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'b', '0');
        auto slotKey1 = fixedBytes(32, 'x', '1');
        auto slotKey2 = fixedBytes(32, 'x', '2');
        auto slotKey3 = fixedBytes(32, 'x', '3');

        auto slotRoot = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey1), makeEntry("base"));
        BOOST_REQUIRE(slotRoot);
        auto baseRoot =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
        BOOST_REQUIRE(baseRoot);

        auto stateKey1 = executor_v1::StateKey(address, slotKey1);
        auto stateKey2 = executor_v1::StateKey(address, slotKey2);
        auto stateKey3 = executor_v1::StateKey(address, slotKey3);

        auto branchRoot1 = co_await storage.writeOne(stateKey2, makeEntry("branch-1"), *baseRoot);
        BOOST_REQUIRE(branchRoot1);
        auto branchRoot2 = co_await storage.writeOne(stateKey3, makeEntry("branch-2"), *baseRoot);
        BOOST_REQUIRE(branchRoot2);

        auto baseValue1 = co_await storage.readOne(stateKey1, *baseRoot);
        BOOST_REQUIRE(baseValue1);
        BOOST_CHECK_EQUAL(baseValue1->get(), "base");
        BOOST_CHECK(!(co_await storage.readOne(stateKey2, *baseRoot)));
        BOOST_CHECK(!(co_await storage.readOne(stateKey3, *baseRoot)));

        auto branch1Value1 = co_await storage.readOne(stateKey1, *branchRoot1);
        BOOST_REQUIRE(branch1Value1);
        BOOST_CHECK_EQUAL(branch1Value1->get(), "base");
        auto branch1Value2 = co_await storage.readOne(stateKey2, *branchRoot1);
        BOOST_REQUIRE(branch1Value2);
        BOOST_CHECK_EQUAL(branch1Value2->get(), "branch-1");
        BOOST_CHECK(!(co_await storage.readOne(stateKey3, *branchRoot1)));

        auto branch2Value1 = co_await storage.readOne(stateKey1, *branchRoot2);
        BOOST_REQUIRE(branch2Value1);
        BOOST_CHECK_EQUAL(branch2Value1->get(), "base");
        BOOST_CHECK(!(co_await storage.readOne(stateKey2, *branchRoot2)));
        auto branch2Value3 = co_await storage.readOne(stateKey3, *branchRoot2);
        BOOST_REQUIRE(branch2Value3);
        BOOST_CHECK_EQUAL(branch2Value3->get(), "branch-2");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadMixedAccountFieldAndSlotAcrossRoots)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'c', '9');
        auto slotKey = fixedBytes(32, 's', '9');
        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto nonceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
        auto slotStateKey = executor_v1::StateKey(address, slotKey);

        auto slotRoot =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey), makeEntry("slot-v1"));
        BOOST_REQUIRE(slotRoot);
        auto root1 =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
        BOOST_REQUIRE(root1);

        auto root2 = co_await storage.writeOne(balanceKey, makeEntry("100"), *root1);
        BOOST_REQUIRE(root2);
        auto root3 = co_await storage.writeOne(nonceKey, makeEntry("7"), *root2);
        BOOST_REQUIRE(root3);

        auto slotValueRoot3 = co_await storage.readOne(slotStateKey, *root3);
        BOOST_REQUIRE(slotValueRoot3);
        BOOST_CHECK_EQUAL(slotValueRoot3->get(), "slot-v1");
        auto balanceRoot3 = co_await storage.readOne(balanceKey, *root3);
        BOOST_REQUIRE(balanceRoot3);
        BOOST_CHECK_EQUAL(balanceRoot3->get(), "100");
        auto nonceRoot3 = co_await storage.readOne(nonceKey, *root3);
        BOOST_REQUIRE(nonceRoot3);
        BOOST_CHECK_EQUAL(nonceRoot3->get(), "7");

        auto root4 = co_await storage.writeOne(slotStateKey, makeEntry("slot-v2"), *root3);
        BOOST_REQUIRE(root4);

        auto slotValueRoot4 = co_await storage.readOne(slotStateKey, *root4);
        BOOST_REQUIRE(slotValueRoot4);
        BOOST_CHECK_EQUAL(slotValueRoot4->get(), "slot-v2");
        auto balanceRoot4 = co_await storage.readOne(balanceKey, *root4);
        BOOST_REQUIRE(balanceRoot4);
        BOOST_CHECK_EQUAL(balanceRoot4->get(), "100");
        auto nonceRoot4 = co_await storage.readOne(nonceKey, *root4);
        BOOST_REQUIRE(nonceRoot4);
        BOOST_CHECK_EQUAL(nonceRoot4->get(), "7");

        auto oldSlotValue = co_await storage.readOne(slotStateKey, *root3);
        BOOST_REQUIRE(oldSlotValue);
        BOOST_CHECK_EQUAL(oldSlotValue->get(), "slot-v1");
    }());
}

BOOST_AUTO_TEST_CASE(writeReadHistoricalRootsForSequentialSlotUpdates)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'd', '4');
        std::vector<std::string> slotKeys = {fixedBytes(32, 'p', '1'), fixedBytes(32, 'p', '2'),
            fixedBytes(32, 'p', '3'), fixedBytes(32, 'p', '4')};

        auto slotRoot =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKeys[0]), makeEntry("v1"));
        BOOST_REQUIRE(slotRoot);
        auto initialRoot =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
        BOOST_REQUIRE(initialRoot);

        std::vector<MPTNodeHash> roots;
        roots.push_back(*initialRoot);

        auto updatedRoot1 = co_await storage.writeOne(
            executor_v1::StateKey(address, slotKeys[1]), makeEntry("v2"), roots.back());
        BOOST_REQUIRE(updatedRoot1);
        roots.push_back(*updatedRoot1);

        auto updatedRoot2 = co_await storage.writeOne(
            executor_v1::StateKey(address, slotKeys[2]), makeEntry("v3"), roots.back());
        BOOST_REQUIRE(updatedRoot2);
        roots.push_back(*updatedRoot2);

        auto updatedRoot3 = co_await storage.writeOne(
            executor_v1::StateKey(address, slotKeys[3]), makeEntry("v4"), roots.back());
        BOOST_REQUIRE(updatedRoot3);
        roots.push_back(*updatedRoot3);

        for (size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex)
        {
            for (size_t slotIndex = 0; slotIndex < slotKeys.size(); ++slotIndex)
            {
                auto value = co_await storage.readOne(
                    executor_v1::StateKey(address, slotKeys[slotIndex]), roots[rootIndex]);

                if (slotIndex <= rootIndex)
                {
                    BOOST_REQUIRE(value);
                    BOOST_CHECK_EQUAL(value->get(), "v" + std::to_string(slotIndex + 1));
                }
                else
                {
                    BOOST_CHECK(!value);
                }
            }
        }

        auto latestBackend = co_await storage.readOne(executor_v1::StateKey(address, slotKeys[3]));
        BOOST_REQUIRE(latestBackend);
        BOOST_CHECK_EQUAL(latestBackend->get(), "v4");
    }());
}

BOOST_AUTO_TEST_CASE(removeUsesLogicalDeletionByDefault)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'e', '1');
        auto slotKey = fixedBytes(32, 'r', '1');
        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto slotStateKey = executor_v1::StateKey(address, slotKey);

        auto slotRoot =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey), makeEntry("slot-v1"));
        BOOST_REQUIRE(slotRoot);
        auto root0 = co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
        BOOST_REQUIRE(root0);

        auto root1 = co_await storage.writeOne(balanceKey, makeEntry("100"), *root0);
        BOOST_REQUIRE(root1);
        auto root3 = co_await storage.removeOne(balanceKey, *root1);
        BOOST_REQUIRE(root3);

        auto oldBalance = co_await storage.readOne(balanceKey, *root1);
        BOOST_REQUIRE(oldBalance);
        BOOST_CHECK_EQUAL(oldBalance->get(), "100");
        BOOST_CHECK(!(co_await storage.readOne(balanceKey, *root3)));

        auto slotValue = co_await storage.readOne(slotStateKey, *root3);
        BOOST_REQUIRE(slotValue);
        BOOST_CHECK_EQUAL(slotValue->get(), "slot-v1");

        auto backendRead = co_await storage.readOne(balanceKey);
        BOOST_CHECK(!backendRead);

        auto range = co_await storage2::range(backendStorage);
        bool sawDeleted = false;
        while (auto item = co_await range.next())
        {
            auto const& [key, value] = *item;
            if (key == balanceKey)
            {
                sawDeleted = std::holds_alternative<storage2::DELETED_TYPE>(value);
            }
        }
        BOOST_CHECK(sawDeleted);
    }());
}

BOOST_AUTO_TEST_CASE(removeDirectUsesPhysicalDeletion)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'f', '2');
        auto slotKey = fixedBytes(32, 't', '2');
        auto slotStateKey = executor_v1::StateKey(address, slotKey);

        auto slotRoot =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey), makeEntry("slot-v1"));
        BOOST_REQUIRE(slotRoot);
        auto root1 = co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address), *slotRoot);
        BOOST_REQUIRE(root1);
        auto root2 = co_await storage.removeOne(slotStateKey, *root1, DIRECT);
        BOOST_REQUIRE(root2);

        auto oldValue = co_await storage.readOne(slotStateKey, *root1);
        BOOST_REQUIRE(oldValue);
        BOOST_CHECK_EQUAL(oldValue->get(), "slot-v1");
        BOOST_CHECK(!(co_await storage.readOne(slotStateKey, *root2)));
        BOOST_CHECK(!(co_await storage.readOne(slotStateKey)));

        auto range = co_await storage2::range(backendStorage);
        bool keyStillExists = false;
        while (auto item = co_await range.next())
        {
            auto const& key = std::get<0>(*item);
            if (key == slotStateKey)
            {
                keyStillExists = true;
            }
        }
        BOOST_CHECK(!keyStillExists);
    }());
}

BOOST_AUTO_TEST_CASE(writeSomeAndRemoveSomeReturnChainedStateRoot)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address1 = fixedBytes(20, 'w', '1');
        auto address2 = fixedBytes(20, 'w', '2');
        auto slotKey1 = fixedBytes(32, 'y', '1');
        auto slotKey2 = fixedBytes(32, 'y', '2');

        auto slotRoot1 =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey1), makeEntry("init-1"));
        BOOST_REQUIRE(slotRoot1);
        auto root0 =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address1), *slotRoot1);
        BOOST_REQUIRE(root0);
        auto slotRoot2 =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey2), makeEntry("init-2"));
        BOOST_REQUIRE(slotRoot2);
        auto root1 =
            co_await trie.writeStorageRoot(*root0, bcos::toHex(address2), *slotRoot2);
        BOOST_REQUIRE(root1);

        auto balanceKey1 = executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto nonceKey1 = executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
        auto slotStateKey1 = executor_v1::StateKey(address1, slotKey1);
        auto balanceKey2 = executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto slotStateKey2 = executor_v1::StateKey(address2, slotKey2);

        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {balanceKey1, makeEntry("101")},
            {nonceKey1, makeEntry("7")},
            {slotStateKey1, makeEntry("slot-1-v2")},
            {balanceKey2, makeEntry("202")},
            {slotStateKey2, makeEntry("slot-2-v2")},
        };

        auto root2 = co_await storage.writeSome(writes, *root1);
        BOOST_REQUIRE(root2);

        auto balance1 = co_await storage.readOne(balanceKey1, *root2);
        BOOST_REQUIRE(balance1);
        BOOST_CHECK_EQUAL(balance1->get(), "101");
        auto nonce1 = co_await storage.readOne(nonceKey1, *root2);
        BOOST_REQUIRE(nonce1);
        BOOST_CHECK_EQUAL(nonce1->get(), "7");
        auto slot1 = co_await storage.readOne(slotStateKey1, *root2);
        BOOST_REQUIRE(slot1);
        BOOST_CHECK_EQUAL(slot1->get(), "slot-1-v2");
        auto balance2 = co_await storage.readOne(balanceKey2, *root2);
        BOOST_REQUIRE(balance2);
        BOOST_CHECK_EQUAL(balance2->get(), "202");
        auto slot2 = co_await storage.readOne(slotStateKey2, *root2);
        BOOST_REQUIRE(slot2);
        BOOST_CHECK_EQUAL(slot2->get(), "slot-2-v2");

        std::vector<executor_v1::StateKey> removes = {nonceKey1, slotStateKey2};
        auto root3 = co_await storage.removeSome(removes, *root2);
        BOOST_REQUIRE(root3);

        auto oldNonce1 = co_await storage.readOne(nonceKey1, *root2);
        BOOST_REQUIRE(oldNonce1);
        BOOST_CHECK_EQUAL(oldNonce1->get(), "7");
        auto oldSlot2 = co_await storage.readOne(slotStateKey2, *root2);
        BOOST_REQUIRE(oldSlot2);
        BOOST_CHECK_EQUAL(oldSlot2->get(), "slot-2-v2");

        BOOST_CHECK(!(co_await storage.readOne(nonceKey1, *root3)));
        BOOST_CHECK(!(co_await storage.readOne(slotStateKey2, *root3)));

        auto retainedBalance1 = co_await storage.readOne(balanceKey1, *root3);
        BOOST_REQUIRE(retainedBalance1);
        BOOST_CHECK_EQUAL(retainedBalance1->get(), "101");
        auto retainedSlot1 = co_await storage.readOne(slotStateKey1, *root3);
        BOOST_REQUIRE(retainedSlot1);
        BOOST_CHECK_EQUAL(retainedSlot1->get(), "slot-1-v2");
        auto retainedBalance2 = co_await storage.readOne(balanceKey2, *root3);
        BOOST_REQUIRE(retainedBalance2);
        BOOST_CHECK_EQUAL(retainedBalance2->get(), "202");
    }());
}

BOOST_AUTO_TEST_CASE(alternatingBackendReadWriteRemoveOperations)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address1 = fixedBytes(20, 'g', '1');
        auto address2 = fixedBytes(20, 'g', '2');
        auto address3 = fixedBytes(20, 'g', '3');
        auto slotKey1 = fixedBytes(32, 'u', '1');
        auto slotKey2 = fixedBytes(32, 'u', '2');
        auto slotKey3 = fixedBytes(32, 'u', '3');

        std::vector<executor_v1::StateKey> candidates = {
            executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address1, slotKey1),
            executor_v1::StateKey(address1, slotKey2),
            executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address2, slotKey2),
            executor_v1::StateKey(address2, slotKey3),
            executor_v1::StateKey(address3, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address3, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address3, slotKey1),
            executor_v1::StateKey(address3, slotKey3),
        };

        std::map<std::string, std::string> expected;
        size_t operations = 0;

        for (size_t i = 0; i < 60; ++i)
        {
            switch (i % 6)
            {
            case 0:
            {
                std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes;
                for (size_t offset = 0; offset < 2; ++offset)
                {
                    auto const& key = candidates[(i + offset * 3) % candidates.size()];
                    auto value = "batch-" + std::to_string(i) + "-" + std::to_string(offset);
                    writes.emplace_back(key, makeEntry(value));
                    expected[stateKeyId(key)] = value;
                }
                co_await storage.writeSome(writes);
                break;
            }
            case 1:
            {
                std::vector<executor_v1::StateKey> reads;
                for (size_t offset = 0; offset < 3; ++offset)
                {
                    reads.emplace_back(candidates[(i + offset * 5) % candidates.size()]);
                }
                auto values = co_await storage.readSome(reads);
                for (size_t index = 0; index < reads.size(); ++index)
                {
                    auto it = expected.find(stateKeyId(reads[index]));
                    if (it == expected.end())
                    {
                        BOOST_CHECK(!values[index]);
                    }
                    else
                    {
                        BOOST_REQUIRE(values[index]);
                        BOOST_CHECK_EQUAL(values[index]->get(), it->second);
                    }
                }
                break;
            }
            case 2:
            {
                auto const& key = candidates[(i * 7) % candidates.size()];
                auto value = "single-" + std::to_string(i);
                co_await storage.writeOne(key, makeEntry(value));
                expected[stateKeyId(key)] = value;
                break;
            }
            case 3:
            {
                auto const& key = candidates[(i * 11 + 1) % candidates.size()];
                auto value = co_await storage.readOne(key);
                auto it = expected.find(stateKeyId(key));
                if (it == expected.end())
                {
                    BOOST_CHECK(!value);
                }
                else
                {
                    BOOST_REQUIRE(value);
                    BOOST_CHECK_EQUAL(value->get(), it->second);
                }
                break;
            }
            case 4:
            {
                std::vector<executor_v1::StateKey> removes;
                for (size_t offset = 0; offset < 2; ++offset)
                {
                    auto const& key = candidates[(i + offset * 4 + 2) % candidates.size()];
                    removes.emplace_back(key);
                    expected.erase(stateKeyId(key));
                }
                co_await storage.removeSome(removes);
                break;
            }
            case 5:
            {
                auto const& key = candidates[(i * 13 + 3) % candidates.size()];
                expected.erase(stateKeyId(key));
                co_await storage.removeOne(key);
                break;
            }
            default:
                BOOST_FAIL("Unexpected operation selector");
            }
            ++operations;
        }

        BOOST_CHECK_GE(operations, 50);

        auto finalValues = co_await storage.readSome(candidates);
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            auto it = expected.find(stateKeyId(candidates[index]));
            if (it == expected.end())
            {
                BOOST_CHECK(!finalValues[index]);
            }
            else
            {
                BOOST_REQUIRE(finalValues[index]);
                BOOST_CHECK_EQUAL(finalValues[index]->get(), it->second);
            }
        }
    }());
}

BOOST_AUTO_TEST_CASE(alternatingStateRootReadWriteRemoveOperations)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address1 = fixedBytes(20, 'h', '1');
        auto address2 = fixedBytes(20, 'h', '2');
        auto slotKey1 = fixedBytes(32, 'v', '1');
        auto slotKey2 = fixedBytes(32, 'v', '2');
        auto slotKey3 = fixedBytes(32, 'v', '3');
        auto slotKey4 = fixedBytes(32, 'v', '4');

        auto slotRoot1 =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey1), makeEntry("init-a1"));
        BOOST_REQUIRE(slotRoot1);
        auto root0 =
            co_await trie.writeStorageRoot(emptyRoot(), bcos::toHex(address1), *slotRoot1);
        BOOST_REQUIRE(root0);
        auto slotRoot2 =
            co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey2), makeEntry("init-b2"));
        BOOST_REQUIRE(slotRoot2);
        auto root1 =
            co_await trie.writeStorageRoot(*root0, bcos::toHex(address2), *slotRoot2);
        BOOST_REQUIRE(root1);

        std::vector<executor_v1::StateKey> candidates = {
            executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address1, slotKey1),
            executor_v1::StateKey(address1, slotKey3),
            executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address2, slotKey2),
            executor_v1::StateKey(address2, slotKey4),
        };

        std::map<std::string, std::string> expected = {
            {stateKeyId(candidates[2]), "init-a1"},
            {stateKeyId(candidates[6]), "init-b2"},
        };

        MPTNodeHash currentRoot = *root1;
        size_t operations = 0;
        std::vector<std::pair<MPTNodeHash, std::map<std::string, std::string>>> snapshots;
        snapshots.emplace_back(currentRoot, expected);

        for (size_t i = 0; i < 60; ++i)
        {
            if (i % 3 == 0)
            {
                auto const& key = candidates[(i * 5 + 1) % candidates.size()];
                auto value = "root-write-" + std::to_string(i);
                auto updatedRoot = co_await storage.writeOne(key, makeEntry(value), currentRoot);
                BOOST_REQUIRE(updatedRoot);
                currentRoot = *updatedRoot;
                expected[stateKeyId(key)] = value;
            }
            else if (i % 3 == 1)
            {
                auto const& key = candidates[(i * 7 + 2) % candidates.size()];
                auto value = co_await storage.readOne(key, currentRoot);
                auto it = expected.find(stateKeyId(key));
                if (it == expected.end())
                {
                    BOOST_CHECK(!value);
                }
                else
                {
                    BOOST_REQUIRE(value);
                    BOOST_CHECK_EQUAL(value->get(), it->second);
                }
            }
            else
            {
                executor_v1::StateKey const* existingKey = nullptr;
                for (auto const& candidate : candidates)
                {
                    if (expected.contains(stateKeyId(candidate)))
                    {
                        existingKey = &candidate;
                        break;
                    }
                }
                BOOST_REQUIRE(existingKey != nullptr);
                auto updatedRoot = co_await storage.removeOne(*existingKey, currentRoot);
                BOOST_REQUIRE(updatedRoot);
                expected.erase(stateKeyId(*existingKey));
                currentRoot = *updatedRoot;
            }

            ++operations;
            if ((i + 1) % 15 == 0)
            {
                snapshots.emplace_back(currentRoot, expected);
            }
        }

        BOOST_CHECK_GE(operations, 50);

        for (auto const& [root, snapshot] : snapshots)
        {
            for (auto const& candidate : candidates)
            {
                auto value = co_await storage.readOne(candidate, root);
                auto it = snapshot.find(stateKeyId(candidate));
                if (it == snapshot.end())
                {
                    BOOST_CHECK(!value);
                }
                else
                {
                    BOOST_REQUIRE(value);
                    BOOST_CHECK_EQUAL(value->get(), it->second);
                }
            }
        }
    }());
}

BOOST_AUTO_TEST_CASE(writeSomeStateRootPreservesHistoricalRoots)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'i', '1');
        auto slotKey1 = fixedBytes(32, 'z', '1');
        auto slotKey2 = fixedBytes(32, 'z', '2');
        auto root0 = co_await bootstrapRootWithSlot(trie, address, slotKey1, "base-slot");

        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto nonceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::NONCE);
        auto slotStateKey1 = executor_v1::StateKey(address, slotKey1);
        auto slotStateKey2 = executor_v1::StateKey(address, slotKey2);

        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {balanceKey, makeEntry("300")},
            {nonceKey, makeEntry("9")},
            {slotStateKey1, makeEntry("slot-1-v2")},
            {slotStateKey2, makeEntry("slot-2-v1")},
        };
        auto root1 = co_await storage.writeSome(writes, root0);
        BOOST_REQUIRE(root1);

        BOOST_CHECK(!(co_await storage.readOne(balanceKey, root0)));
        auto oldSlot1 = co_await storage.readOne(slotStateKey1, root0);
        BOOST_REQUIRE(oldSlot1);
        BOOST_CHECK_EQUAL(oldSlot1->get(), "base-slot");

        auto balance = co_await storage.readOne(balanceKey, *root1);
        BOOST_REQUIRE(balance);
        BOOST_CHECK_EQUAL(balance->get(), "300");
        auto nonce = co_await storage.readOne(nonceKey, *root1);
        BOOST_REQUIRE(nonce);
        BOOST_CHECK_EQUAL(nonce->get(), "9");
        auto newSlot1 = co_await storage.readOne(slotStateKey1, *root1);
        BOOST_REQUIRE(newSlot1);
        BOOST_CHECK_EQUAL(newSlot1->get(), "slot-1-v2");
        auto newSlot2 = co_await storage.readOne(slotStateKey2, *root1);
        BOOST_REQUIRE(newSlot2);
        BOOST_CHECK_EQUAL(newSlot2->get(), "slot-2-v1");
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeStateRootDirectRemovesMultipleBranches)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address1 = fixedBytes(20, 'j', '1');
        auto address2 = fixedBytes(20, 'j', '2');
        auto slotKey1 = fixedBytes(32, 'm', '1');
        auto slotKey2 = fixedBytes(32, 'm', '2');

        auto root0 = co_await bootstrapRootWithSlot(trie, address1, slotKey1, "a-slot");
        auto slotRoot2 = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey2), makeEntry("b-slot"));
        BOOST_REQUIRE(slotRoot2);
        auto root1 = co_await trie.writeStorageRoot(root0, bcos::toHex(address2), *slotRoot2);
        BOOST_REQUIRE(root1);

        auto stateKey1 = executor_v1::StateKey(address1, slotKey1);
        auto stateKey2 = executor_v1::StateKey(address2, slotKey2);
        auto root2 = co_await storage.removeSome(std::vector{stateKey1, stateKey2}, *root1, DIRECT);
        BOOST_REQUIRE(root2);

        BOOST_CHECK(!(co_await storage.readOne(stateKey1, *root2)));
        BOOST_CHECK(!(co_await storage.readOne(stateKey2, *root2)));
        BOOST_CHECK(!(co_await storage.readOne(stateKey1)));
        BOOST_CHECK(!(co_await storage.readOne(stateKey2)));
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeLogicalDeletionLeavesMultipleTombstones)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address = fixedBytes(20, 'k', '7');
        std::vector<executor_v1::StateKey> keys = {
            executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
            executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::ALIVE),
        };
        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {keys[0], makeEntry("1")},
            {keys[1], makeEntry("2")},
            {keys[2], makeEntry("3")},
        };
        co_await storage.writeSome(writes);
        co_await storage.removeSome(keys);

        auto range = co_await storage2::range(backendStorage);
        size_t deletedCount = 0;
        while (auto item = co_await range.next())
        {
            auto const& [key, value] = *item;
            if (::ranges::find(keys, key) != keys.end() &&
                std::holds_alternative<storage2::DELETED_TYPE>(value))
            {
                ++deletedCount;
            }
        }
        BOOST_CHECK_EQUAL(deletedCount, keys.size());
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeDirectLeavesNoTombstones)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address = fixedBytes(20, 'l', '8');
        std::vector<executor_v1::StateKey> keys = {
            executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE),
            executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::NONCE),
        };
        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {keys[0], makeEntry("11")},
            {keys[1], makeEntry("22")},
        };
        co_await storage.writeSome(writes);
        co_await storage.removeSome(keys, DIRECT);

        auto range = co_await storage2::range(backendStorage);
        while (auto item = co_await range.next())
        {
            auto const& key = std::get<0>(*item);
            BOOST_CHECK(::ranges::find(keys, key) == keys.end());
        }
    }());
}

BOOST_AUTO_TEST_CASE(writeSomeStateRootReturnsNulloptForMissingStorageRootSlotWrite)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address = fixedBytes(20, 'm', '1');
        auto slotKey = fixedBytes(32, 'n', '1');
        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto slotStateKey = executor_v1::StateKey(address, slotKey);

        auto root0 = co_await storage.writeOne(balanceKey, makeEntry("100"), emptyRoot());
        BOOST_REQUIRE(root0);

        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {slotStateKey, makeEntry("slot-v1")},
        };
        auto root1 = co_await storage.writeSome(writes, *root0);
        BOOST_CHECK(!root1);

        auto backendSlot = co_await storage.readOne(slotStateKey);
        BOOST_REQUIRE(backendSlot);
        BOOST_CHECK_EQUAL(backendSlot->get(), "slot-v1");
        BOOST_CHECK(!(co_await storage.readOne(slotStateKey, *root0)));
    }());
}

BOOST_AUTO_TEST_CASE(removeOneStateRootReturnsNulloptForMissingAccountField)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address = fixedBytes(20, 'n', '2');
        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto nonceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::NONCE);

        auto root0 = co_await storage.writeOne(balanceKey, makeEntry("500"), emptyRoot());
        BOOST_REQUIRE(root0);
        auto root1 = co_await storage.removeOne(nonceKey, *root0);
        BOOST_CHECK(!root1);

        auto balance = co_await storage.readOne(balanceKey, *root0);
        BOOST_REQUIRE(balance);
        BOOST_CHECK_EQUAL(balance->get(), "500");
        BOOST_CHECK(!(co_await storage.readOne(nonceKey)));
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeStateRootReturnsNulloptForMissingSlot)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'o', '3');
        auto slotKey1 = fixedBytes(32, 'q', '1');
        auto slotKey2 = fixedBytes(32, 'q', '2');
        auto root0 = co_await bootstrapRootWithSlot(trie, address, slotKey1, "slot-1");
        auto missingKey = executor_v1::StateKey(address, slotKey2);

        auto root1 = co_await storage.removeSome(std::vector{missingKey}, root0);
        BOOST_CHECK(!root1);
        BOOST_CHECK(!(co_await storage.readOne(missingKey, root0)));
    }());
}

BOOST_AUTO_TEST_CASE(writeOneStateRootCanRecreateAfterLogicalRemove)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address = fixedBytes(20, 'p', '4');
        auto balanceKey = executor_v1::StateKey(address, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto root0 = co_await storage.writeOne(balanceKey, makeEntry("1"), emptyRoot());
        BOOST_REQUIRE(root0);
        auto root1 = co_await storage.removeOne(balanceKey, *root0);
        BOOST_REQUIRE(root1);
        auto root2 = co_await storage.writeOne(balanceKey, makeEntry("2"), *root1);
        BOOST_REQUIRE(root2);

        BOOST_CHECK(!(co_await storage.readOne(balanceKey, *root1)));
        auto value = co_await storage.readOne(balanceKey, *root2);
        BOOST_REQUIRE(value);
        BOOST_CHECK_EQUAL(value->get(), "2");
    }());
}

BOOST_AUTO_TEST_CASE(removeDirectThenRewriteSlotOnHistoricalRoot)
{
    task::syncWait([]() -> task::Task<void> {
        LogicalBackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<LogicalBackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address = fixedBytes(20, 'r', '5');
        auto slotKey = fixedBytes(32, 's', '5');
        auto root0 = co_await bootstrapRootWithSlot(trie, address, slotKey, "slot-old");
        auto stateKey = executor_v1::StateKey(address, slotKey);

        auto root1 = co_await storage.removeOne(stateKey, root0, DIRECT);
        BOOST_REQUIRE(root1);
        auto root2 = co_await storage.writeOne(stateKey, makeEntry("slot-new"), root0);
        BOOST_REQUIRE(root2);

        BOOST_CHECK(!(co_await storage.readOne(stateKey, *root1)));
        auto value = co_await storage.readOne(stateKey, *root2);
        BOOST_REQUIRE(value);
        BOOST_CHECK_EQUAL(value->get(), "slot-new");
    }());
}

BOOST_AUTO_TEST_CASE(writeSomeMultipleAddressesFieldsOnlyChainsCorrectly)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, std::make_shared<crypto::Keccak256>());

        auto address1 = fixedBytes(20, 't', '1');
        auto address2 = fixedBytes(20, 't', '2');
        auto address3 = fixedBytes(20, 't', '3');

        std::vector<std::tuple<executor_v1::StateKey, storage::Entry>> writes = {
            {executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::BALANCE), makeEntry("10")},
            {executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::NONCE), makeEntry("11")},
            {executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::BALANCE), makeEntry("20")},
            {executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::NONCE), makeEntry("21")},
            {executor_v1::StateKey(address3, ledger::ACCOUNT_TABLE_FIELDS::BALANCE), makeEntry("30")},
            {executor_v1::StateKey(address3, ledger::ACCOUNT_TABLE_FIELDS::NONCE), makeEntry("31")},
        };

        auto root = co_await storage.writeSome(writes, emptyRoot());
        BOOST_REQUIRE(root);

        for (auto const& [key, value] : writes)
        {
            auto readValue = co_await storage.readOne(key, *root);
            BOOST_REQUIRE(readValue);
            BOOST_CHECK_EQUAL(readValue->get(), value.get());
        }
    }());
}

BOOST_AUTO_TEST_CASE(removeSomeStateRootMixedFieldsAndSlotsAcrossAddresses)
{
    task::syncWait([]() -> task::Task<void> {
        BackendStorage backendStorage;
        NodeStorage nodeStorage;
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        MerkleStorage<BackendStorage, NodeStorage> storage(
            backendStorage, nodeStorage, hashImpl);
        MPT<NodeStorage> trie(nodeStorage, hashImpl);

        auto address1 = fixedBytes(20, 'u', '1');
        auto address2 = fixedBytes(20, 'u', '2');
        auto slotKey1 = fixedBytes(32, 'w', '1');
        auto slotKey2 = fixedBytes(32, 'w', '2');

        auto root0 = co_await bootstrapRootWithSlot(trie, address1, slotKey1, "slot-a");
        auto slotRoot2 = co_await trie.writeSlot(emptyRoot(), bcos::toHex(slotKey2), makeEntry("slot-b"));
        BOOST_REQUIRE(slotRoot2);
        auto root1 = co_await trie.writeStorageRoot(root0, bcos::toHex(address2), *slotRoot2);
        BOOST_REQUIRE(root1);

        auto balanceKey1 = executor_v1::StateKey(address1, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto balanceKey2 = executor_v1::StateKey(address2, ledger::ACCOUNT_TABLE_FIELDS::BALANCE);
        auto slotStateKey1 = executor_v1::StateKey(address1, slotKey1);
        auto slotStateKey2 = executor_v1::StateKey(address2, slotKey2);

        auto root2 = co_await storage.writeSome(std::vector{
                std::tuple{balanceKey1, makeEntry("101")},
                std::tuple{balanceKey2, makeEntry("202")}},
            *root1);
        BOOST_REQUIRE(root2);

        auto root3 = co_await storage.removeSome(std::vector{balanceKey1, slotStateKey2}, *root2);
        BOOST_REQUIRE(root3);

        BOOST_CHECK(!(co_await storage.readOne(balanceKey1, *root3)));
        BOOST_CHECK(!(co_await storage.readOne(slotStateKey2, *root3)));
        auto retainBalance2 = co_await storage.readOne(balanceKey2, *root3);
        BOOST_REQUIRE(retainBalance2);
        BOOST_CHECK_EQUAL(retainBalance2->get(), "202");
        auto retainSlot1 = co_await storage.readOne(slotStateKey1, *root3);
        BOOST_REQUIRE(retainSlot1);
        BOOST_CHECK_EQUAL(retainSlot1->get(), "slot-a");
    }());
}

BOOST_AUTO_TEST_SUITE_END()
