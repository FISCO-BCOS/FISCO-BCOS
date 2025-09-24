/**
 *  Copyright (C) 2025.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <fakeit.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

struct MemoryStorageFixture
{
    MemoryStorageFixture()
      : txValidator(&mockValidator.get(), [](bcos::txpool::TxValidatorInterface*) {}),
        txPoolNonceChecker(&mockNonceChecker.get(), [](bcos::txpool::NonceCheckerInterface*) {}),
        config(std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, nullptr,
            txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ 1024, /*checkSig*/ false)),
        storage(config)
    {
        using namespace fakeit;
        When(Method(mockValidator, checkTransaction))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);

        // Web3NonceChecker: return a usable instance (internal structures are in-memory only; pass
        // nullptr for ledger)
        auto web3Checker = std::make_shared<bcos::txpool::Web3NonceChecker>(nullptr);
        When(Method(mockValidator, web3NonceChecker)).AlwaysReturn(web3Checker);

        // txPool NonceChecker: set all methods to no-op (side-effect free) implementations
        When(Method(mockNonceChecker, checkNonce))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);
        When(Method(mockNonceChecker, exists)).AlwaysReturn(false);
        When(Method(mockNonceChecker, batchInsert)).AlwaysDo([](auto, auto const&) {});
        When(
            OverloadedMethod(mockNonceChecker, batchRemove, void(bcos::protocol::NonceList const&)))
            .AlwaysDo([](auto const&) {});
        When(OverloadedMethod(mockNonceChecker, batchRemove,
                 void(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
                     std::hash<bcos::protocol::NonceType>> const&)))
            .AlwaysDo([](auto const&) {});
        When(Method(mockNonceChecker, insert)).AlwaysDo([](auto const&) {});
        When(Method(mockNonceChecker, remove)).AlwaysDo([](auto const&) {});
    }

    // Create a simple transaction and compute its hash
    bcostars::protocol::TransactionImpl::Ptr makeTx(std::string nonce, bool sealed)
    {
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
        tx->setNonce(std::move(nonce));
        tx->setSealed(sealed);
        Keccak256 keccak;
        tx->calculateHash(keccak);
        return tx;
    }

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txpool::NonceCheckerInterface> mockNonceChecker;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txpool::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<TxPoolConfig> config;
    MemoryStorage storage;
};

BOOST_FIXTURE_TEST_SUITE(TxpoolMemoryStorageTest, MemoryStorageFixture)

BOOST_AUTO_TEST_CASE(InsertExistsAndSize)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    auto tx2 = makeTx("n2", /*sealed*/ true);

    BOOST_CHECK(storage.insert(tx1) == TransactionStatus::None);
    BOOST_CHECK(storage.insert(tx2) == TransactionStatus::None);

    BOOST_CHECK_EQUAL(storage.exists(tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage.exists(tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage.size(), 2U);

    // getTransactions
    HashList hashes{tx1->hash(), tx2->hash()};
    auto out = storage.getTransactions(hashes);
    BOOST_CHECK_EQUAL(out.size(), 2U);
    BOOST_CHECK(out[0]);
    BOOST_CHECK(out[1]);
    BOOST_CHECK_EQUAL(out[0]->hash(), tx1->hash());
    BOOST_CHECK_EQUAL(out[1]->hash(), tx2->hash());
}

BOOST_AUTO_TEST_CASE(FilterUnknownAndBatchExists)
{
    auto tx1 = makeTx("m1", false);
    auto tx2 = makeTx("m2", true);
    storage.insert(tx1);
    storage.insert(tx2);

    HashType missing{};  // all zeros, non-existent
    HashList query{tx1->hash(), missing, tx2->hash()};

    auto miss = storage.filterUnknownTxs(query, nullptr);
    BOOST_CHECK_EQUAL(miss.size(), 1U);
    BOOST_CHECK_EQUAL(miss[0], missing);

    // batchExists: returns false if any is missing; true if all exist
    BOOST_CHECK_EQUAL(storage.batchExists(query), false);
    HashList allHave{tx1->hash(), tx2->hash()};
    BOOST_CHECK_EQUAL(storage.batchExists(allHave), true);
}

BOOST_AUTO_TEST_CASE(BatchMarkSealAndUnseal)
{
    // Insert 3 unsealed transactions first
    std::vector<bcostars::protocol::TransactionImpl::Ptr> txs;
    for (int i = 0; i < 3; ++i)
    {
        auto tx = makeTx("s" + std::to_string(i), false);
        storage.insert(tx);
        txs.push_back(tx);
    }

    HashList toSeal{txs[0]->hash(), txs[1]->hash(), txs[2]->hash()};
    HashType batchHash;  // arbitrary value
    auto ok = storage.batchMarkTxs(toSeal, /*batchId*/ 1, batchHash, /*_sealFlag*/ true);
    BOOST_CHECK_EQUAL(ok, true);
    // Verify transactions are marked as sealed
    for (auto& tx : txs)
    {
        BOOST_CHECK_EQUAL(tx->sealed(), true);
        BOOST_CHECK_EQUAL(storage.exists(tx->hash()), true);
    }

    // Unseal two of them (must use the same batchId/batchHash as sealing)
    HashList unseal{txs[1]->hash(), txs[2]->hash()};
    ok = storage.batchMarkTxs(unseal, /*batchId*/ 1, batchHash, /*_sealFlag*/ false);
    BOOST_CHECK_EQUAL(ok, true);
    BOOST_CHECK_EQUAL(txs[0]->sealed(), true);
    BOOST_CHECK_EQUAL(txs[1]->sealed(), false);
    BOOST_CHECK_EQUAL(txs[2]->sealed(), false);
}

BOOST_AUTO_TEST_CASE(RemoveAndClear)
{
    auto tx = makeTx("r1", false);
    storage.insert(tx);
    BOOST_CHECK_EQUAL(storage.exists(tx->hash()), true);

    storage.remove(tx->hash());
    BOOST_CHECK_EQUAL(storage.exists(tx->hash()), false);

    // Insert two more transactions and then clear
    storage.insert(makeTx("r2", false));
    storage.insert(makeTx("r3", true));
    BOOST_CHECK(storage.size() >= 2);
    storage.clear();
    BOOST_CHECK_EQUAL(storage.size(), 0U);
}

BOOST_AUTO_TEST_CASE(GetTxsHash)
{
    // Iterate unsealed transactions only
    std::vector<HashType> inserted;
    for (int i = 0; i < 5; ++i)
    {
        auto tx = makeTx("h" + std::to_string(i), false);
        inserted.emplace_back(tx->hash());
        storage.insert(tx);
    }

    auto hashesPtr = storage.getTxsHash(100);
    BOOST_REQUIRE(hashesPtr);
    auto& hashes = *hashesPtr;
    // Should at least contain the hashes we inserted (iteration order is not guaranteed)
    for (auto const& h : inserted)
    {
        auto it = std::find(hashes.begin(), hashes.end(), h);
        BOOST_CHECK(it != hashes.end());
    }
}

BOOST_AUTO_TEST_SUITE_END()
