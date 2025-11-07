/**
 *  Copyright (C) 2025.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/storage2/AnyStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <fakeit.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

struct MemoryStorageFixture
{
    MemoryStorageFixture()
      : txValidator(&mockValidator.get(), [](bcos::txpool::TxValidatorInterface*) {}),
        txPoolNonceChecker(&mockNonceChecker.get(), [](bcos::txpool::NonceCheckerInterface*) {}),
        ledgerNonceChecker(&mockLedgerNonceChecker.get(), [](bcos::txpool::LedgerNonceChecker*) {}),
        cryptoSuite(
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr)),
        blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory)),
        config(std::make_shared<TxPoolConfig>(txValidator,
            std::make_shared<bcostars::protocol::TransactionSubmitResultFactoryImpl>(),
            blockFactory, nullptr, txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ 1024,
            /*checkSig*/ false)),
        storage(std::make_shared<MemoryStorage>(config))
    {
        fakeit::When(Method(mockValidator, checkTransaction))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);

        // Web3NonceChecker: return a usable instance (internal structures are in-memory only; pass
        // nullptr for ledger)
        auto web3Checker = std::make_shared<bcos::txpool::Web3NonceChecker>(nullptr);
        fakeit::When(Method(mockValidator, web3NonceChecker)).AlwaysReturn(web3Checker);

        // LedgerNonceChecker: set all methods to no-op implementations
        fakeit::When(Method(mockValidator, ledgerNonceChecker)).AlwaysReturn(ledgerNonceChecker);
        fakeit::When(Method(mockLedgerNonceChecker, batchInsert)).AlwaysDo([](auto, auto const&) {
        });

        // txPool NonceChecker: set all methods to no-op (side-effect free) implementations
        fakeit::When(Method(mockNonceChecker, checkNonce))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);
        fakeit::When(Method(mockNonceChecker, exists)).AlwaysReturn(false);
        fakeit::When(Method(mockNonceChecker, batchInsert)).AlwaysDo([](auto, auto const&) {});
        fakeit::When(
            OverloadedMethod(mockNonceChecker, batchRemove, void(bcos::protocol::NonceList const&)))
            .AlwaysDo([](auto const&) {});
        fakeit::When(OverloadedMethod(mockNonceChecker, batchRemove,
                         void(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
                             std::hash<bcos::protocol::NonceType>> const&)))
            .AlwaysDo([](auto const&) {});
        fakeit::When(Method(mockNonceChecker, insert)).AlwaysDo([](auto const&) {});
        fakeit::When(Method(mockNonceChecker, remove)).AlwaysDo([](auto const&) {});
    }

    // Create a simple transaction and compute its hash
    bcostars::protocol::TransactionImpl::Ptr makeTx(std::string nonce, bool sealed)
    {
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
        tx->setNonce(std::move(nonce));
        tx->setSealed(sealed);
        tx->setImportTime(utcTime());
        Keccak256 keccak;
        tx->calculateHash(keccak);
        return tx;
    }

    // Create a Web3 transaction with specific nonce and sender
    bcostars::protocol::TransactionImpl::Ptr makeWeb3Tx(
        std::string nonce, std::string senderHex, bool sealed)
    {
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
        tx->setNonce(std::move(nonce));
        tx->mutableInner().type =
            static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
        // Convert hex string to bytes for sender
        auto senderBytes = fromHexWithPrefix(senderHex);
        tx->mutableInner().sender.assign(senderBytes.begin(), senderBytes.end());
        HashType txHash = HashType::generateRandomFixedBytes();
        tx->mutableInner().extraTransactionHash.assign(txHash.begin(), txHash.end());
        tx->setSealed(sealed);
        Keccak256 keccak;
        tx->setImportTime(utcTime());
        tx->calculateHash(keccak);
        return tx;
    }

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txpool::NonceCheckerInterface> mockNonceChecker;
    fakeit::Mock<bcos::txpool::LedgerNonceChecker> mockLedgerNonceChecker;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txpool::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<bcos::txpool::LedgerNonceChecker> ledgerNonceChecker;
    // Added tars protocol factories and BlockFactoryImpl for config
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::protocol::BlockHeaderFactory::Ptr blockHeaderFactory;
    bcos::protocol::TransactionFactory::Ptr transactionFactory;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory;
    bcos::protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<TxPoolConfig> config;
    std::shared_ptr<MemoryStorage> storage;
};

BOOST_FIXTURE_TEST_SUITE(TxpoolMemoryStorageTest, MemoryStorageFixture)

BOOST_AUTO_TEST_CASE(InsertExistsAndSize)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    auto tx2 = makeTx("n2", /*sealed*/ true);
    BOOST_CHECK(storage->insert(tx1) == TransactionStatus::None);
    BOOST_CHECK(storage->insert(tx2) == TransactionStatus::None);

    BOOST_CHECK_EQUAL(storage->exists(tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 2U);

    // getTransactions
    HashList hashes{tx1->hash(), tx2->hash()};
    auto out = storage->getTransactions(hashes);
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
    storage->insert(tx1);
    storage->insert(tx2);

    HashType missing{};  // all zeros, non-existent
    HashList query{tx1->hash(), missing, tx2->hash()};

    auto miss = storage->filterUnknownTxs(query, nullptr);
    BOOST_CHECK_EQUAL(miss.size(), 1U);
    BOOST_CHECK_EQUAL(miss[0], missing);

    // batchExists: returns false if any is missing; true if all exist
    BOOST_CHECK_EQUAL(storage->batchExists(query), false);
    HashList allHave{tx1->hash(), tx2->hash()};
    BOOST_CHECK_EQUAL(storage->batchExists(allHave), true);
}

BOOST_AUTO_TEST_CASE(BatchMarkSealAndUnseal)
{
    // Insert 3 unsealed transactions first
    std::vector<bcostars::protocol::TransactionImpl::Ptr> txs;
    for (int i = 0; i < 3; ++i)
    {
        auto tx = makeTx("s" + std::to_string(i), false);
        storage->insert(tx);
        txs.push_back(tx);
    }

    HashList toSeal{txs[0]->hash(), txs[1]->hash(), txs[2]->hash()};
    HashType batchHash;  // arbitrary value
    auto ok = storage->batchMarkTxs(toSeal, /*batchId*/ 1, batchHash, /*_sealFlag*/ true);
    BOOST_CHECK_EQUAL(ok, true);
    // Verify transactions are marked as sealed
    for (auto& tx : txs)
    {
        BOOST_CHECK_EQUAL(tx->sealed(), true);
        BOOST_CHECK_EQUAL(storage->exists(tx->hash()), true);
    }

    // Unseal two of them (must use the same batchId/batchHash as sealing)
    HashList unseal{txs[1]->hash(), txs[2]->hash()};
    ok = storage->batchMarkTxs(unseal, /*batchId*/ 1, batchHash, /*_sealFlag*/ false);
    BOOST_CHECK_EQUAL(ok, true);
    BOOST_CHECK_EQUAL(txs[0]->sealed(), true);
    BOOST_CHECK_EQUAL(txs[1]->sealed(), false);
    BOOST_CHECK_EQUAL(txs[2]->sealed(), false);
}

BOOST_AUTO_TEST_CASE(RemoveAndClear)
{
    auto tx = makeTx("r1", false);
    storage->insert(tx);
    BOOST_CHECK_EQUAL(storage->exists(tx->hash()), true);

    storage->remove(tx->hash());
    BOOST_CHECK_EQUAL(storage->exists(tx->hash()), false);

    // Insert two more transactions and then clear
    storage->insert(makeTx("r2", false));
    storage->insert(makeTx("r3", true));
    BOOST_CHECK(storage->size() >= 2);
    storage->clear();
    BOOST_CHECK_EQUAL(storage->size(), 0U);
}

BOOST_AUTO_TEST_CASE(GetTxsHash)
{
    // Iterate unsealed transactions only
    std::vector<HashType> inserted;
    for (int i = 0; i < 5; ++i)
    {
        auto tx = makeTx("h" + std::to_string(i), false);
        inserted.emplace_back(tx->hash());
        storage->insert(tx);
    }

    auto hashesPtr = storage->getTxsHash(100);
    BOOST_REQUIRE(hashesPtr);
    auto& hashes = *hashesPtr;
    // Should at least contain the hashes we inserted (iteration order is not guaranteed)
    for (auto const& h : inserted)
    {
        auto it = std::find(hashes.begin(), hashes.end(), h);
        BOOST_CHECK(it != hashes.end());
    }
}

BOOST_AUTO_TEST_CASE(BatchRemoveSealedTxsWithWeb3Transactions)
{
    // This test verifies that batchRemoveSealedTxs correctly updates Web3 transaction nonces
    // when transactions are removed after being sealed (addressing the synchronization issue)

    // Create test data: Web3 transactions with different senders and nonces
    const std::string sender1Hex = "0x1234567890123456789012345678901234567890";
    const std::string sender2Hex = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    // Create Web3 transactions with different nonces
    const auto web3Tx1 = makeWeb3Tx("5", sender1Hex, true);  // sealed, nonce 5
    const auto web3Tx2 = makeWeb3Tx("7", sender1Hex, true);  // sealed, nonce 7
    const auto web3Tx3 = makeWeb3Tx("3", sender2Hex, true);  // sealed, nonce 3

    // Create a BCOS transaction (for comparison)
    const auto bcosTx = makeTx("bcos_nonce_1", true);

    // Insert transactions into storage
    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(web3Tx3);
    storage->insert(bcosTx);

    // Verify transactions exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Create TransactionSubmitResults for the transactions
    TransactionSubmitResults txsResult;

    const auto result1 = std::make_shared<TransactionSubmitResultImpl>();
    result1->setTxHash(web3Tx1->hash());
    result1->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result1);

    const auto result2 = std::make_shared<TransactionSubmitResultImpl>();
    result2->setTxHash(web3Tx2->hash());
    result2->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result2);

    const auto result3 = std::make_shared<TransactionSubmitResultImpl>();
    result3->setTxHash(web3Tx3->hash());
    result3->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result3);

    const auto result4 = std::make_shared<TransactionSubmitResultImpl>();
    result4->setTxHash(bcosTx->hash());
    result4->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    result4->setNonce(std::string(bcosTx->nonce()));
    txsResult.push_back(result4);

    // Call batchRemoveSealedTxs - this should:
    // 1. Remove the transactions from sealed storage
    // 2. Update Web3 nonce cache for sender1 and sender2
    // 3. Update ledger nonce for BCOS transaction
    BlockNumber batchId = 100;
    storage->batchRemoveSealedTxs(batchId, txsResult);

    // Verify transactions have been removed from storage
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), false);
    BOOST_CHECK_EQUAL(storage->size(), 0U);

    // The key part of the test: verify that Web3NonceChecker was called with correct data
    // The web3NonceChecker should have been updated with:
    // - sender1: nonces {5, 7} -> max nonce 7+1=8
    // - sender2: nonce {3} -> max nonce 3+1=4

    // Verify the web3 nonce cache was updated correctly by checking pending nonce
    const auto web3Checker = config->txValidator()->web3NonceChecker();

    // After removing sealed txs with nonce 5 and 7 for sender1,
    // the ledger nonce should be updated to 8 (7+1)
    // Note: getPendingNonce expects hex string format
    const auto pendingNonce1 = task::syncWait(web3Checker->getPendingNonce(sender1Hex));
    BOOST_CHECK(pendingNonce1.has_value());
    if (pendingNonce1.has_value())
    {
        // The pending nonce should be the max nonce + 1 = 7 + 1 = 8
        BOOST_CHECK_EQUAL(pendingNonce1.value(), 8);
    }

    // For sender2, pending nonce should be 4 (3+1)
    const auto pendingNonce2 = task::syncWait(web3Checker->getPendingNonce(sender2Hex));
    BOOST_CHECK(pendingNonce2.has_value());
    if (pendingNonce2.has_value())
    {
        BOOST_CHECK_EQUAL(pendingNonce2.value(), 4);
    }

    // test sync block scenario
    const auto web3Tx4 =
        makeWeb3Tx("9", sender1Hex, true);  // sealed, nonce 9 (higher than previous 7)
    const auto web3Tx5 =
        makeWeb3Tx("4", sender2Hex, true);  // sealed, nonce 4 (higher than previous 3)

    storage->insert(web3Tx4);
    storage->insert(web3Tx5);

    // Create results for sync block
    TransactionSubmitResults syncTxsResult;

    auto syncResult1 = std::make_shared<TransactionSubmitResultImpl>();
    syncResult1->setTxHash(web3Tx4->hash());
    syncResult1->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    syncTxsResult.push_back(syncResult1);

    auto syncResult2 = std::make_shared<TransactionSubmitResultImpl>();
    syncResult2->setTxHash(web3Tx5->hash());
    syncResult2->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    syncTxsResult.push_back(syncResult2);

    // Remove synced transactions
    BlockNumber syncBatchId = 101;
    storage->batchRemoveSealedTxs(syncBatchId, syncTxsResult);

    // Verify new pending nonces after sync
    // For sender1, pending nonce should now be 10 (9+1)
    const auto pendingNonceAfterSync1 = task::syncWait(web3Checker->getPendingNonce(sender1Hex));
    BOOST_CHECK(pendingNonceAfterSync1.has_value());
    if (pendingNonceAfterSync1.has_value())
    {
        BOOST_CHECK_EQUAL(pendingNonceAfterSync1.value(), 10);
    }

    // For sender2, pending nonce should now be 5 (4+1)
    const auto pendingNonceAfterSync2 = task::syncWait(web3Checker->getPendingNonce(sender2Hex));
    BOOST_CHECK(pendingNonceAfterSync2.has_value());
    if (pendingNonceAfterSync2.has_value())
    {
        BOOST_CHECK_EQUAL(pendingNonceAfterSync2.value(), 5);
    }
}

BOOST_AUTO_TEST_CASE(BatchRemoveSealedTxsMixedTypes)
{
    // Test with a mix of Web3 and BCOS transactions to ensure both types are handled correctly

    const std::string web3SenderHex = "0x9876543210987654321098765432109876543210";

    // Create mixed transaction types
    const auto web3Tx1 = makeWeb3Tx("10", web3SenderHex, true);  // nonce 10
    const auto web3Tx2 = makeWeb3Tx("12", web3SenderHex, true);  // nonce 12
    const auto bcosTx1 = makeTx("bcos_n1", true);
    const auto bcosTx2 = makeTx("bcos_n2", true);

    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(bcosTx1);
    storage->insert(bcosTx2);

    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Create results
    TransactionSubmitResults txsResult;

    auto result1 = std::make_shared<TransactionSubmitResultImpl>();
    result1->setTxHash(web3Tx1->hash());
    result1->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result1);

    auto result2 = std::make_shared<TransactionSubmitResultImpl>();
    result2->setTxHash(web3Tx2->hash());
    result2->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result2);

    auto result3 = std::make_shared<TransactionSubmitResultImpl>();
    result3->setTxHash(bcosTx1->hash());
    result3->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    result3->setNonce(std::string(bcosTx1->nonce()));
    txsResult.push_back(result3);

    auto result4 = std::make_shared<TransactionSubmitResultImpl>();
    result4->setTxHash(bcosTx2->hash());
    result4->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    result4->setNonce(std::string(bcosTx2->nonce()));
    txsResult.push_back(result4);

    // Remove all transactions
    BlockNumber batchId = 200;
    storage->batchRemoveSealedTxs(batchId, txsResult);

    // Verify all removed
    BOOST_CHECK_EQUAL(storage->size(), 0U);
}

// ==================== Tests for Web3Transactions Integration ====================

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsInsertAndExists)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string sender1Hex = "0x1234567890123456789012345678901234567890";
    const std::string sender2Hex = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    // Create Web3 transactions
    auto web3Tx1 = makeWeb3Tx("1", sender1Hex, false);
    auto web3Tx2 = makeWeb3Tx("2", sender1Hex, false);
    auto web3Tx3 = makeWeb3Tx("1", sender2Hex, false);

    TXPOOL_LOG(DEBUG) << "Inserting Web3 Transactions:" << web3Tx1->hash() << ", "
                      << web3Tx2->hash() << ", " << web3Tx3->hash();

    // Insert Web3 transactions
    BOOST_CHECK(storage->insert(web3Tx1) == TransactionStatus::None);
    BOOST_CHECK(storage->insert(web3Tx2) == TransactionStatus::None);
    BOOST_CHECK(storage->insert(web3Tx3) == TransactionStatus::None);

    // Verify Web3 transactions exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), true);

    // Create a BCOS transaction for comparison
    auto bcosTx = makeTx("bcos_nonce", false);
    BOOST_CHECK(storage->insert(bcosTx) == TransactionStatus::None);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsGetTransactions)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0x1111111111111111111111111111111111111111";

    // Create mixed transaction types
    auto web3Tx1 = makeWeb3Tx("5", senderHex, false);
    auto web3Tx2 = makeWeb3Tx("6", senderHex, true);
    auto bcosTx1 = makeTx("bcos1", false);
    auto bcosTx2 = makeTx("bcos2", true);

    // Insert transactions
    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(bcosTx1);
    storage->insert(bcosTx2);

    // Get all transactions by hash
    HashList hashes{web3Tx1->hash(), web3Tx2->hash(), bcosTx1->hash(), bcosTx2->hash()};
    auto transactions = storage->getTransactions(hashes);

    BOOST_REQUIRE_EQUAL(transactions.size(), 4U);

    // Verify all transactions are retrieved correctly
    BOOST_CHECK(transactions[0] != nullptr);
    BOOST_CHECK(transactions[1] != nullptr);
    BOOST_CHECK(transactions[2] != nullptr);
    BOOST_CHECK(transactions[3] != nullptr);

    BOOST_CHECK_EQUAL(transactions[0]->hash(), web3Tx1->hash());
    BOOST_CHECK_EQUAL(transactions[1]->hash(), web3Tx2->hash());
    BOOST_CHECK_EQUAL(transactions[2]->hash(), bcosTx1->hash());
    BOOST_CHECK_EQUAL(transactions[3]->hash(), bcosTx2->hash());

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsBatchExists)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0x2222222222222222222222222222222222222222";

    // Create transactions
    auto web3Tx1 = makeWeb3Tx("1", senderHex, false);
    auto web3Tx2 = makeWeb3Tx("2", senderHex, false);
    auto bcosTx = makeTx("bcos", false);

    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(bcosTx);

    // Test batchExists with all existing transactions
    HashList allExist{web3Tx1->hash(), web3Tx2->hash(), bcosTx->hash()};
    BOOST_CHECK_EQUAL(storage->batchExists(allExist), true);

    // Test batchExists with one missing transaction
    HashType missingHash = HashType::generateRandomFixedBytes();
    HashList withMissing{web3Tx1->hash(), missingHash, bcosTx->hash()};
    BOOST_CHECK_EQUAL(storage->batchExists(withMissing), false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsVerifyAndSubmit)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0x3333333333333333333333333333333333333333";

    // Create a Web3 transaction
    auto web3Tx = makeWeb3Tx("10", senderHex, false);

    // Verify and submit the Web3 transaction
    auto status = storage->verifyAndSubmitTransaction(
        web3Tx, nullptr, /*checkPoolLimit*/ false, /*lock*/ false);

    BOOST_CHECK_EQUAL(status, TransactionStatus::None);
    BOOST_CHECK(storage->exists(web3Tx->hash()));

    // Create another Web3 transaction with different nonce
    auto web3Tx2 = makeWeb3Tx("11", senderHex, false);
    status = storage->verifyAndSubmitTransaction(
        web3Tx2, nullptr, /*checkPoolLimit*/ false, /*lock*/ false);

    BOOST_CHECK_EQUAL(status, TransactionStatus::None);
    BOOST_CHECK(storage->exists(web3Tx2->hash()));

    // Both transactions should be in the pool
    BOOST_CHECK(storage->size() >= 2);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsBatchRemove)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string sender1Hex = "0x4444444444444444444444444444444444444444";
    const std::string sender2Hex = "0x5555555555555555555555555555555555555555";

    // Create Web3 transactions with different nonces
    auto web3Tx1 = makeWeb3Tx("1", sender1Hex, true);
    auto web3Tx2 = makeWeb3Tx("2", sender1Hex, true);
    auto web3Tx3 = makeWeb3Tx("3", sender1Hex, true);
    auto web3Tx4 = makeWeb3Tx("1", sender2Hex, true);

    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(web3Tx3);
    storage->insert(web3Tx4);

    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Create TransactionSubmitResults
    TransactionSubmitResults txsResult;

    auto result1 = std::make_shared<TransactionSubmitResultImpl>();
    result1->setTxHash(web3Tx1->hash());
    result1->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result1);

    auto result2 = std::make_shared<TransactionSubmitResultImpl>();
    result2->setTxHash(web3Tx2->hash());
    result2->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result2);

    auto result3 = std::make_shared<TransactionSubmitResultImpl>();
    result3->setTxHash(web3Tx3->hash());
    result3->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result3);

    auto result4 = std::make_shared<TransactionSubmitResultImpl>();
    result4->setTxHash(web3Tx4->hash());
    result4->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result4);

    // Remove sealed transactions
    BlockNumber batchId = 1;
    storage->batchRemoveSealedTxs(batchId, txsResult);

    // Verify transactions are removed
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx4->hash()), false);
    BOOST_CHECK_EQUAL(storage->size(), 0U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsMixedTypesBatchRemove)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0x6666666666666666666666666666666666666666";

    // Create mixed transaction types
    auto web3Tx1 = makeWeb3Tx("10", senderHex, true);
    auto web3Tx2 = makeWeb3Tx("11", senderHex, true);
    auto bcosTx1 = makeTx("bcos1", true);
    auto bcosTx2 = makeTx("bcos2", true);

    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(bcosTx1);
    storage->insert(bcosTx2);

    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Create results for all transactions
    TransactionSubmitResults txsResult;

    auto result1 = std::make_shared<TransactionSubmitResultImpl>();
    result1->setTxHash(web3Tx1->hash());
    result1->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result1);

    auto result2 = std::make_shared<TransactionSubmitResultImpl>();
    result2->setTxHash(web3Tx2->hash());
    result2->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    txsResult.push_back(result2);

    auto result3 = std::make_shared<TransactionSubmitResultImpl>();
    result3->setTxHash(bcosTx1->hash());
    result3->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    result3->setNonce(std::string(bcosTx1->nonce()));
    txsResult.push_back(result3);

    auto result4 = std::make_shared<TransactionSubmitResultImpl>();
    result4->setTxHash(bcosTx2->hash());
    result4->setStatus(static_cast<uint32_t>(TransactionStatus::None));
    result4->setNonce(std::string(bcosTx2->nonce()));
    txsResult.push_back(result4);

    // Remove all transactions
    constexpr BlockNumber batchId = 10;
    storage->batchRemoveSealedTxs(batchId, txsResult);

    // Verify all are removed
    BOOST_CHECK_EQUAL(storage->size(), 0U);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx1->hash()), false);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx2->hash()), false);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsNonceOrdering)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0x7777777777777777777777777777777777777777";

    // Create Web3 transactions with non-sequential nonces (out of order insertion)
    auto web3Tx1 = makeWeb3Tx("5", senderHex, false);  // nonce 5
    auto web3Tx2 = makeWeb3Tx("3", senderHex, false);  // nonce 3
    auto web3Tx3 = makeWeb3Tx("4", senderHex, false);  // nonce 4
    auto web3Tx4 = makeWeb3Tx("6", senderHex, false);  // nonce 6

    // Insert in non-sequential order
    storage->insert(web3Tx1);
    storage->insert(web3Tx2);
    storage->insert(web3Tx3);
    storage->insert(web3Tx4);

    // All transactions should exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx4->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Get transactions - they should be retrievable regardless of insertion order
    HashList hashes{web3Tx1->hash(), web3Tx2->hash(), web3Tx3->hash(), web3Tx4->hash()};
    auto transactions = storage->getTransactions(hashes);

    BOOST_REQUIRE_EQUAL(transactions.size(), 4U);
    BOOST_CHECK(transactions[0] != nullptr);
    BOOST_CHECK(transactions[1] != nullptr);
    BOOST_CHECK(transactions[2] != nullptr);
    BOOST_CHECK(transactions[3] != nullptr);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsEmptyBatchOperations)
{
    // Enable Web3Transactions support
    storage->setEnableWeb3Transactions(true);

    // Test batchExists with empty hash list
    HashList emptyHashes;
    BOOST_CHECK_EQUAL(storage->batchExists(emptyHashes), true);

    // Test getTransactions with empty hash list
    auto transactions = storage->getTransactions(emptyHashes);
    BOOST_CHECK_EQUAL(transactions.size(), 0U);

    // Test batchRemoveSealedTxs with empty results
    TransactionSubmitResults emptyResults;
    constexpr BlockNumber batchId = 100;
    storage->batchRemoveSealedTxs(batchId, emptyResults);

    BOOST_CHECK_EQUAL(storage->size(), 0U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(EnableWeb3TransactionsToggleFeature)
{
    const std::string senderHex = "0x9999999999999999999999999999999999999999";

    // Initially disabled - insert BCOS transaction
    storage->setEnableWeb3Transactions(false);
    auto bcosTx = makeTx("bcos", false);
    storage->insert(bcosTx);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);

    // Enable Web3Transactions
    storage->setEnableWeb3Transactions(true);

    // Insert Web3 transaction
    auto web3Tx = makeWeb3Tx("1", senderHex, false);
    storage->insert(web3Tx);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx->hash()), true);

    // Both transactions should exist
    BOOST_CHECK_EQUAL(storage->size(), 2U);

    // Disable Web3Transactions again
    storage->setEnableWeb3Transactions(false);

    // BCOS transaction should still exist
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);

    // Web3 transaction existence depends on implementation
    // (it might still exist in m_web3Transactions but not be checked when disabled)

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(SubmitTransactionWithWeb3Enabled)
{
    // Test submitTransaction with Web3Transactions enabled
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    // Create Web3 transaction
    auto web3Tx = makeWeb3Tx("5", senderHex, false);

    // Submit transaction without waiting for receipt
    auto submitResult = task::syncWait(storage->submitTransaction(web3Tx, false));

    BOOST_CHECK(submitResult != nullptr);
    BOOST_CHECK_EQUAL(submitResult->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK_EQUAL(submitResult->txHash(), web3Tx->hash());

    // Verify transaction is in storage
    BOOST_CHECK_EQUAL(storage->exists(web3Tx->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 1U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(SubmitTransactionWithWeb3Disabled)
{
    // Test submitTransaction with Web3Transactions disabled
    storage->setEnableWeb3Transactions(false);

    // Create BCOS transaction
    auto bcosTx = makeTx("nonce_123", false);

    // Submit transaction
    auto submitResult = task::syncWait(storage->submitTransaction(bcosTx, false));

    BOOST_CHECK(submitResult != nullptr);
    BOOST_CHECK_EQUAL(submitResult->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK_EQUAL(submitResult->txHash(), bcosTx->hash());

    // Verify transaction is in storage
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 1U);
}

BOOST_AUTO_TEST_CASE(SubmitMultipleWeb3TransactionsSameSender)
{
    // Test submitting multiple Web3 transactions from the same sender
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    // Submit transactions with sequential nonces
    auto web3Tx1 = makeWeb3Tx("1", senderHex, false);
    auto web3Tx2 = makeWeb3Tx("2", senderHex, false);
    auto web3Tx3 = makeWeb3Tx("3", senderHex, false);

    auto result1 = task::syncWait(storage->submitTransaction(web3Tx1, false));
    auto result2 = task::syncWait(storage->submitTransaction(web3Tx2, false));
    auto result3 = task::syncWait(storage->submitTransaction(web3Tx3, false));

    // All submissions should succeed
    BOOST_CHECK(result1 != nullptr);
    BOOST_CHECK_EQUAL(result1->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK(result2 != nullptr);
    BOOST_CHECK_EQUAL(result2->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK(result3 != nullptr);
    BOOST_CHECK_EQUAL(result3->status(), static_cast<uint32_t>(TransactionStatus::None));

    // All transactions should exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx3->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 3U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(SubmitWeb3TransactionDuplicateHash)
{
    // Test submitting the same Web3 transaction twice
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xcccccccccccccccccccccccccccccccccccccccc";
    auto web3Tx = makeWeb3Tx("10", senderHex, false);

    // First submission should succeed
    auto result1 = task::syncWait(storage->submitTransaction(web3Tx, false));
    BOOST_CHECK(result1 != nullptr);
    BOOST_CHECK_EQUAL(result1->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK_EQUAL(storage->size(), 1U);

    // Second submission with same transaction should be handled appropriately
    // (might return AlreadyInTxPool status)
    auto result2 = task::syncWait(storage->submitTransaction(web3Tx, false));
    BOOST_CHECK(result2 != nullptr);

    // Storage size should remain 1
    BOOST_CHECK_EQUAL(storage->size(), 1U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(SubmitMixedTransactionTypes)
{
    // Test submitting both Web3 and BCOS transactions when Web3 is enabled
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xdddddddddddddddddddddddddddddddddddddddd";

    // Submit Web3 transaction
    auto web3Tx = makeWeb3Tx("7", senderHex, false);
    auto web3Result = task::syncWait(storage->submitTransaction(web3Tx, false));

    // Submit BCOS transaction
    auto bcosTx = makeTx("bcos_nonce", false);
    auto bcosResult = task::syncWait(storage->submitTransaction(bcosTx, false));

    // Both should succeed
    BOOST_CHECK(web3Result != nullptr);
    BOOST_CHECK_EQUAL(web3Result->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK(bcosResult != nullptr);
    BOOST_CHECK_EQUAL(bcosResult->status(), static_cast<uint32_t>(TransactionStatus::None));

    // Both transactions should exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(bcosTx->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 2U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(SubmitWeb3TransactionMultipleSenders)
{
    // Test submitting Web3 transactions from different senders
    storage->setEnableWeb3Transactions(true);

    const std::string sender1Hex = "0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    const std::string sender2Hex = "0xffffffffffffffffffffffffffffffffffffffff";

    // Submit transactions from different senders with same nonce
    auto web3Tx1 = makeWeb3Tx("1", sender1Hex, false);
    auto web3Tx2 = makeWeb3Tx("1", sender2Hex, false);

    auto result1 = task::syncWait(storage->submitTransaction(web3Tx1, false));
    auto result2 = task::syncWait(storage->submitTransaction(web3Tx2, false));

    // Both should succeed (different senders)
    BOOST_CHECK(result1 != nullptr);
    BOOST_CHECK_EQUAL(result1->status(), static_cast<uint32_t>(TransactionStatus::None));
    BOOST_CHECK(result2 != nullptr);
    BOOST_CHECK_EQUAL(result2->status(), static_cast<uint32_t>(TransactionStatus::None));

    // Both transactions should exist
    BOOST_CHECK_EQUAL(storage->exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage->exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage->size(), 2U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3Disabled)
{
    // Test the second batchSealTransactions (with state parameter) when Web3 is disabled
    storage->setEnableWeb3Transactions(false);

    // Create and insert some BCOS transactions
    constexpr int numBcosTxs = 5;
    std::vector<protocol::Transaction::Ptr> txs;
    for (int i = 0; i < numBcosTxs; ++i)
    {
        auto tx = makeTx("nonce_" + std::to_string(i), false);
        storage->insert(tx);
        txs.push_back(tx);
    }

    BOOST_CHECK_EQUAL(storage->size(), 5U);

    // Create a mock state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    // Prepare output vectors
    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    // Call batchSealTransactions with state
    constexpr size_t limit = 10;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    // Should succeed
    BOOST_CHECK_EQUAL(result, true);

    // Since Web3 is disabled, only BCOS transactions should be sealed
    BOOST_CHECK(txsList.size() > 0);
    BOOST_CHECK(txsList.size() <= 5);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3Enabled)
{
    // Test the second batchSealTransactions with Web3Transactions enabled
    storage->setEnableWeb3Transactions(true);

    const std::string sender1Hex = "0xa1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1";
    const std::string sender2Hex = "0xb2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2";

    // Create BCOS transactions
    std::vector<protocol::Transaction::Ptr> bcosTxs;
    for (int i = 0; i < 3; ++i)
    {
        auto tx = makeTx("bcos_" + std::to_string(i), false);
        storage->insert(tx);
        bcosTxs.push_back(tx);
    }

    // Create Web3 transactions
    std::vector<protocol::Transaction::Ptr> web3Txs;
    for (int i = 0; i < 3; ++i)
    {
        auto tx = makeWeb3Tx(std::to_string(i), sender1Hex, false);
        storage->insert(tx);
        web3Txs.push_back(tx);
    }

    // Create Web3 transactions from sender2
    for (int i = 0; i < 2; ++i)
    {
        auto tx = makeWeb3Tx(std::to_string(i), sender2Hex, false);
        storage->insert(tx);
        web3Txs.push_back(tx);
    }

    BOOST_CHECK_EQUAL(storage->size(), 8U);

    // Create a mock state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    // Prepare output vectors
    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    // Call batchSealTransactions with state - request 10 transactions
    constexpr size_t limit = 10;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    // Should succeed
    BOOST_CHECK_EQUAL(result, true);

    // Should seal transactions from both BCOS and Web3 pools
    BOOST_CHECK(txsList.size() > 0);
    // May not seal all due to nonce ordering in Web3Transactions
    BOOST_CHECK(txsList.size() <= 8);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3OnlyBcosFull)
{
    // Test that Web3 transactions are sealed when BCOS pool is exhausted
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xc3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3c3";

    // Create only 2 BCOS transactions
    constexpr int numBcosTxs = 2;
    constexpr int numWeb3Txs = 5;
    for (int i = 0; i < numBcosTxs; ++i)
    {
        auto tx = makeTx("bcos_" + std::to_string(i), false);
        storage->insert(tx);
    }

    // Create multiple Web3 transactions with sequential nonces
    for (int i = 0; i < numWeb3Txs; ++i)
    {
        auto tx = makeWeb3Tx(std::to_string(i), senderHex, false);
        storage->insert(tx);
    }

    BOOST_CHECK_EQUAL(storage->size(), 7U);

    // Create state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    // Request more transactions than BCOS pool has
    constexpr size_t limit = 6;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    BOOST_CHECK_EQUAL(result, true);

    // Should seal BCOS txs first, then Web3 txs
    BOOST_CHECK(txsList.size() > 2);  // More than just BCOS transactions
    BOOST_CHECK(txsList.size() <= 7);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3LimitReached)
{
    // Test that sealing stops when limit is reached
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xd4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4d4";

    // Create many transactions
    constexpr int numTxsPerType = 10;
    for (int i = 0; i < numTxsPerType; ++i)
    {
        auto tx = makeTx("bcos_" + std::to_string(i), false);
        storage->insert(tx);
    }

    for (int i = 0; i < numTxsPerType; ++i)
    {
        auto tx = makeWeb3Tx(std::to_string(i), senderHex, false);
        storage->insert(tx);
    }

    BOOST_CHECK_EQUAL(storage->size(), 20U);

    // Create state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    // Request only 5 transactions
    constexpr size_t limit = 5;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    BOOST_CHECK_EQUAL(result, true);

    // Should seal at most 5 transactions
    size_t totalSealed = txsList.size() + sysTxsList.size();
    BOOST_CHECK(totalSealed <= 5);
    BOOST_CHECK(totalSealed > 0);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3EmptyPool)
{
    // Test batchSealTransactions when both pools are empty
    storage->setEnableWeb3Transactions(true);

    // Create state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    constexpr size_t limit = 10;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    BOOST_CHECK_EQUAL(result, true);

    // No transactions should be sealed
    BOOST_CHECK_EQUAL(txsList.size(), 0U);
    BOOST_CHECK_EQUAL(sysTxsList.size(), 0U);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_CASE(BatchSealTransactionsWithStateWeb3SystemTransactions)
{
    // Test that system transactions are correctly separated
    storage->setEnableWeb3Transactions(true);

    const std::string senderHex = "0xe5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5e5";

    // Create regular Web3 transactions
    for (int i = 0; i < 3; ++i)
    {
        auto tx = makeWeb3Tx(std::to_string(i), senderHex, false);
        storage->insert(tx);
    }

    // Create system transactions (if supported by the transaction type)
    auto sysTx = makeTx("sys_nonce", false);
    sysTx->setSystemTx(true);
    storage->insert(sysTx);

    BOOST_CHECK_EQUAL(storage->size(), 4U);

    // Create state storage
    storage2::memory_storage::MemoryStorage<executor_v1::StateKey, executor_v1::StateValue,
        storage2::memory_storage::Attribute::ORDERED>
        stateStorage;

    storage2::AnyStorage<executor_v1::StateKeyView, executor_v1::StateValue> state(stateStorage);

    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;

    constexpr size_t limit = 10;
    auto result = task::syncWait(storage->batchSealTransactions(limit, state, txsList, sysTxsList));

    BOOST_CHECK_EQUAL(result, true);

    // System transactions should be separated into sysTxsList
    BOOST_CHECK(txsList.size() > 0 || sysTxsList.size() > 0);
    size_t totalSealed = txsList.size() + sysTxsList.size();
    BOOST_CHECK(totalSealed <= 4);

    storage->setEnableWeb3Transactions(false);
}

BOOST_AUTO_TEST_SUITE_END()
