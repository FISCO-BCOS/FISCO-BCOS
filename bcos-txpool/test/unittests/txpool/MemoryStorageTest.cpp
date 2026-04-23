/**
 *  Copyright (C) 2025.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-txpool/txpool/utilities/Common.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/TxPoolNonceChecker.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include "bcos-utilities/DataConvertUtility.h"

#include <sw/redis++/cxx_utils.h>
#include <tbb/parallel_for.h>

#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <future>
#include <thread>
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
        ledgerNonceChecker(&mockLedgerNonceChecker.get(), [](bcos::txpool::LedgerNonceChecker*) {}),
        ledger(&mockLedger.get(), [](bcos::ledger::LedgerInterface*) {}),
        config(std::make_shared<TxPoolConfig>(txValidator,
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>(), nullptr,
            nullptr, txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ 1024,
            /*checkSig*/ false)),
        storage(config)
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
        fakeit::When(Method(mockNonceChecker, insert)).AlwaysDo([](auto const&) { return true; });
        fakeit::When(Method(mockNonceChecker, remove)).AlwaysDo([](auto const&) {});
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
        tx->calculateHash(keccak);
        return tx;
    }

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txpool::NonceCheckerInterface> mockNonceChecker;
    fakeit::Mock<bcos::txpool::LedgerNonceChecker> mockLedgerNonceChecker;
    fakeit::Mock<bcos::ledger::LedgerInterface> mockLedger;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txpool::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<bcos::txpool::LedgerNonceChecker> ledgerNonceChecker;
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger;
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

BOOST_AUTO_TEST_CASE(BatchRemoveSealedTxsWithWeb3Transactions)
{
    // This test verifies that batchRemoveSealedTxs correctly updates Web3 transaction nonces
    // when transactions are removed after being sealed (addressing the synchronization issue)

    // Create test data: Web3 transactions with different senders and nonces
    const std::string sender1Hex = "0x1234567890123456789012345678901234567890";
    const std::string sender2Hex = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    // Create Web3 transactions with different nonces
    const auto web3Tx1 = makeWeb3Tx("0x5", sender1Hex, true);  // sealed, nonce 5
    const auto web3Tx2 = makeWeb3Tx("0x7", sender1Hex, true);  // sealed, nonce 7
    const auto web3Tx3 = makeWeb3Tx("0x3", sender2Hex, true);  // sealed, nonce 3

    // Create a BCOS transaction (for comparison)
    const auto bcosTx = makeTx("bcos_nonce_1", true);

    // Insert transactions into storage
    storage.insert(web3Tx1);
    storage.insert(web3Tx2);
    storage.insert(web3Tx3);
    storage.insert(bcosTx);

    // Verify transactions exist
    BOOST_CHECK_EQUAL(storage.exists(web3Tx1->hash()), true);
    BOOST_CHECK_EQUAL(storage.exists(web3Tx2->hash()), true);
    BOOST_CHECK_EQUAL(storage.exists(web3Tx3->hash()), true);
    BOOST_CHECK_EQUAL(storage.exists(bcosTx->hash()), true);
    BOOST_CHECK_EQUAL(storage.size(), 4U);

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
    storage.batchRemoveSealedTxs(batchId, txsResult);

    // Verify transactions have been removed from storage
    BOOST_CHECK_EQUAL(storage.exists(web3Tx1->hash()), false);
    BOOST_CHECK_EQUAL(storage.exists(web3Tx2->hash()), false);
    BOOST_CHECK_EQUAL(storage.exists(web3Tx3->hash()), false);
    BOOST_CHECK_EQUAL(storage.exists(bcosTx->hash()), false);
    BOOST_CHECK_EQUAL(storage.size(), 0U);

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
        makeWeb3Tx("0x9", sender1Hex, true);  // sealed, nonce 9 (higher than previous 7)
    const auto web3Tx5 =
        makeWeb3Tx("0x4", sender2Hex, true);  // sealed, nonce 4 (higher than previous 3)

    storage.insert(web3Tx4);
    storage.insert(web3Tx5);

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
    storage.batchRemoveSealedTxs(syncBatchId, syncTxsResult);

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
    const auto web3Tx1 = makeWeb3Tx("0xa", web3SenderHex, true);  // nonce 10
    const auto web3Tx2 = makeWeb3Tx("0xc", web3SenderHex, true);  // nonce 12
    const auto bcosTx1 = makeTx("bcos_n1", true);
    const auto bcosTx2 = makeTx("bcos_n2", true);

    storage.insert(web3Tx1);
    storage.insert(web3Tx2);
    storage.insert(bcosTx1);
    storage.insert(bcosTx2);

    BOOST_CHECK_EQUAL(storage.size(), 4U);

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
    storage.batchRemoveSealedTxs(batchId, txsResult);

    // Verify all removed
    BOOST_CHECK_EQUAL(storage.size(), 0U);

    // Verify Web3 nonce updated correctly (max nonce 12, so pending should be 13)
    auto web3Checker = config->txValidator()->web3NonceChecker();
    // Note: getPendingNonce expects hex string format
    auto pendingNonce = task::syncWait(web3Checker->getPendingNonce(web3SenderHex));
    BOOST_CHECK(pendingNonce.has_value());
    if (pendingNonce.has_value())
    {
        BOOST_CHECK_EQUAL(pendingNonce.value(), 13);  // 0xc (12) + 1
    }
}

BOOST_AUTO_TEST_CASE(VerifyAndSubmitTransactionValidationChain)
{
    // Test all validation steps in verifyAndSubmitTransaction
    // This test covers the validation chain pattern we implemented

    // Setup: Create a real validator with proper configuration
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto keyPair = signatureImpl->generateKeyPair();
    std::string groupId = "group_test";
    std::string chainId = "chain_test";

    fakeit::Mock<bcos::txpool::Web3NonceChecker> mockWeb3NonceChecker;
    fakeit::When(Method(mockWeb3NonceChecker, insertMemoryNonce))
        .AlwaysDo([](auto, auto) -> task::Task<bool> { co_return true; });

    std::shared_ptr<bcos::txpool::Web3NonceChecker> web3NonceChecker(
        &mockWeb3NonceChecker.get(), [](bcos::txpool::Web3NonceChecker*) {});
    // Create real validators
    // Create a mock ledger
    fakeit::When(OverloadedMethod(mockWeb3NonceChecker, checkWeb3Nonce,
                     task::Task<TransactionStatus>(const bcos::protocol::Transaction&, bool)))
        .AlwaysDo([](const auto&, auto) -> task::Task<TransactionStatus> {
            co_return TransactionStatus::None;
        });
    fakeit::When(OverloadedMethod(mockWeb3NonceChecker, checkWeb3Nonce,
                     task::Task<TransactionStatus>(std::string_view, std::string_view, bool)))
        .AlwaysDo([](auto, auto, auto) -> task::Task<TransactionStatus> {
            co_return TransactionStatus::None;
        });

    auto txValidator = std::make_shared<TxValidator>(txPoolNonceChecker, web3NonceChecker,
        cryptoSuite, groupId, chainId, std::weak_ptr<bcos::scheduler::SchedulerInterface>{});

    // Create config with signature check enabled
    auto configWithSig = std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, ledger,
        txPoolNonceChecker, /*blockLimit*/ 1000,
        /*poolLimit*/ 1024, /*checkSig*/ true);
    MemoryStorage storageWithSig(configWithSig);

    // Create config with signature check disabled
    auto configNoSig = std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, ledger,
        txPoolNonceChecker, /*blockLimit*/ 1000,
        /*poolLimit*/ 1024, /*checkSig*/ false);
    MemoryStorage storageNoSig(configNoSig);

    // Test 1: Step 1 - AlreadyInTxPool
    {
        auto tx1 = makeTx("nonce1", false);
        storageWithSig.insert(tx1);  // Insert first time
        auto result = storageWithSig.verifyAndSubmitTransaction(tx1, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::AlreadyInTxPool);
    }

    // Test 4: Step 3 - OverFlowValue
    {
        storageNoSig.clear();
        auto tx4 = makeWeb3Tx("1235", "0xd485BAEE65E501F1cDa071a5b5c9327C401dcD5a", false);
        // Set a value that exceeds MAX_LENGTH - need to cast to TransactionImpl
        auto tx4Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx4);
        if (tx4Impl)
        {
            std::string largeValue(TRANSACTION_VALUE_MAX_LENGTH + 1, '1');
            tx4Impl->mutableInner().data.value.assign(largeValue.begin(), largeValue.end());
        }
        auto result = storageNoSig.verifyAndSubmitTransaction(tx4, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::OverFlowValue);
    }

    // Test 5: Step 3 - MaxInitCodeSizeExceeded (for Web3Transaction)
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx5 = makeWeb3Tx("0x1", senderHex, false);
        // Set input size larger than MAX_INITCODE_SIZE - need to cast to TransactionImpl
        auto tx5Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx5);
        if (tx5Impl)
        {
            std::string largeInput(MAX_INITCODE_SIZE + 1, '1');
            tx5Impl->mutableInner().data.input.assign(largeInput.begin(), largeInput.end());
        }
        auto result = storageNoSig.verifyAndSubmitTransaction(tx5, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::MaxInitCodeSizeExceeded);
    }

    // // Test 6: Step 4 - InsufficientFunds
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx6 = makeWeb3Tx("0x2", senderHex, false);
        // Set a large value - need to cast to TransactionImpl
        auto tx6Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx6);
        if (tx6Impl)
        {
            std::string largeValue = "0x1000000000000000000000000";  // Very large value
            tx6Impl->mutableInner().data.value.assign(largeValue.begin(), largeValue.end());
        }

        fakeit::When(Method(mockLedger, asyncGetSystemConfigByKey))
            .AlwaysDo(
                [](auto const&,
                    std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                    callback(nullptr, "0x1234", 0);
                });

        fakeit::When(Method(mockLedger, asyncGetBlockNumber)).AlwaysDo([](auto) -> long long {
            return 0;
        });

        auto result = storageNoSig.verifyAndSubmitTransaction(tx6, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::InsufficientFunds);
    }

    // Test 7: Step 5 - InvalidChainId (for Web3Transaction)
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx7 = makeWeb3Tx("0x3", senderHex, false);
        // Set an invalid chainId - need to cast to TransactionImpl
        auto tx7Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx7);
        if (tx7Impl)
        {
            std::string invalidChainId = "123";
            tx7Impl->mutableInner().data.chainID = invalidChainId;
        }

        fakeit::When(Method(mockLedger, asyncGetSystemConfigByKey))
            .AlwaysDo(
                [](auto const& key,
                    std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                    if (key == ledger::SYSTEM_KEY_WEB3_CHAIN_ID)
                    {
                        callback(nullptr, "321", 0);
                    }
                    else if (key == ledger::SYSTEM_KEY_TX_GAS_PRICE)
                    {
                        callback(nullptr, "0", 0);
                    }
                });
        auto result = storageNoSig.verifyAndSubmitTransaction(tx7, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::InvalidChainId);
    }

    // Test 8: Success case - All validations pass
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx8 = makeWeb3Tx("0x4", senderHex, false);
        // Set a small value and valid chainId - need to cast to TransactionImpl
        auto tx8Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx8);
        if (tx8Impl)
        {
            std::string smallValue = "0x100";
            tx8Impl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            // Set valid chainId (empty or matching)
            tx8Impl->mutableInner().data.chainID = "";
        }

        // Mock ledger to return nullptr (simplified test)
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);

        // Setup validator to pass all checks
        auto ledgerNonceChecker = std::make_shared<LedgerNonceChecker>(
            nullptr, /*blockNumber*/ 0, /*blockLimit*/ 1000, /*checkBlockLimit*/ false);
        txValidator->setLedgerNonceChecker(ledgerNonceChecker);

        auto result = storageNoSig.verifyAndSubmitTransaction(tx8, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::None);
        // Note: Result may vary depending on balance validation and other checks
        // The test verifies the validation chain executes without crashing
    }

    // Test 9: Validation chain stops at first failure
    {
        storageNoSig.clear();
        auto tx9 = makeTx("nonce9", false);
        // Set both invalid value and insert it first to trigger AlreadyInTxPool
        storageNoSig.insert(tx9);
        auto tx9Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx9);
        if (tx9Impl)
        {
            std::string largeValue(TRANSACTION_VALUE_MAX_LENGTH + 1, '1');
            tx9Impl->mutableInner().data.value.assign(largeValue.begin(), largeValue.end());
        }
        // Should fail at Step 1 (AlreadyInTxPool), not at Step 3 (OverFlowValue)
        auto result = storageNoSig.verifyAndSubmitTransaction(tx9, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::AlreadyInTxPool);
    }

    // Test 10: Signature check is skipped when disabled
    {
        storageNoSig.clear();
        auto tx10 = makeTx("nonce10", false);
        // Even with invalid signature, should pass Step 2 when checkSig is false
        auto tx10Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx10);
        if (tx10Impl)
        {
            auto corruptedSig = tx10->signatureData();
            if (!corruptedSig.empty())
            {
                bcos::bytes sigBytes(corruptedSig.begin(), corruptedSig.end());
                sigBytes[0] ^= 0xFF;
                tx10Impl->setSignatureData(sigBytes);
            }
        }
        // Should proceed to next steps (might fail at other steps, but not at signature)
        auto result = storageNoSig.verifyAndSubmitTransaction(tx10, nullptr, false, false);
        // Result depends on other validation steps
    }
}

BOOST_AUTO_TEST_CASE(FIB61_NegativeImportTimeTreatedAsExpired)
{
    // FIB-61: A tx with negative importTime caused unsigned overflow when computing
    // importTime + m_txsExpirationTime, potentially treating an expired tx as valid.
    // The fix adds an explicit `importTime < 0` guard in batchSealTransactions.

    // Set up a real BlockFactory required by batchSealTransactions
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);
    config->setBlockFactory(blockFactory);

    // tx1: negative import time — must be treated as expired
    auto tx1 = makeTx("fib61_expired", false);
    tx1->setImportTime(-1);
    storage.insert(tx1);

    // tx2: current time — must be included in sealed output
    auto tx2 = makeTx("fib61_valid", false);
    tx2->setImportTime(static_cast<int64_t>(utcTime()));
    storage.insert(tx2);

    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;
    storage.batchSealTransactions(txsList, sysTxsList, 100);

    bool foundTx1 = false;
    bool foundTx2 = false;
    for (auto& meta : txsList)
    {
        if (meta->hash() == tx1->hash())
            foundTx1 = true;
        if (meta->hash() == tx2->hash())
            foundTx2 = true;
    }
    // tx1 must be excluded (treated as expired due to negative importTime)
    BOOST_CHECK(!foundTx1);
    // tx2 must be included
    BOOST_CHECK(foundTx2);
}

BOOST_AUTO_TEST_CASE(FIB51_TxPoolNonceCheckerInsertReturnsBool)
{
    // FIB-51: TxPoolNonceChecker::insert() now returns bool (true = newly inserted,
    // false = already existed). This makes the check-and-reserve atomic per bucket,
    // eliminating the TOCTOU window between separate checkNonce() + insert() calls.

    TxPoolNonceChecker checker;

    // First insert: nonce is new -> must return true
    const std::string nonce1 = "fib51_nonce_unique";
    BOOST_CHECK(checker.insert(nonce1) == true);

    // Second insert of the same nonce: already exists -> must return false
    BOOST_CHECK(checker.insert(nonce1) == false);

    // Different nonce: returns true again
    const std::string nonce2 = "fib51_nonce_other";
    BOOST_CHECK(checker.insert(nonce2) == true);

    // Concurrent test: 50 threads all insert the same nonce; exactly one must succeed
    const std::string raceNonce = "fib51_race_nonce";
    std::atomic<int> successCount{0};
    tbb::parallel_for(tbb::blocked_range<int>(0, 50), [&](const tbb::blocked_range<int>& range) {
        for (int i = range.begin(); i < range.end(); ++i)
        {
            if (checker.insert(raceNonce))
            {
                successCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    BOOST_CHECK_EQUAL(successCount.load(), 1);
}

BOOST_AUTO_TEST_CASE(FIB55_PoolLimitEnforced)
{
    // FIB-55: Pool limit was bypassed because the check happened after expensive validation.
    // The fix moves the pool limit check (Step 1.5) before signature verification and nonce
    // checks so that TxPoolIsFull is returned early.
    constexpr size_t kLimit = 3;
    auto limitedConfig = std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, nullptr,
        txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ kLimit, /*checkSig*/ false);
    MemoryStorage limitedStorage(limitedConfig);

    // Insert kLimit txs directly (bypasses validator, fills the pool)
    for (size_t i = 0; i < kLimit; ++i)
    {
        auto tx = makeTx("fib55_n" + std::to_string(i), false);
        BOOST_CHECK_EQUAL(limitedStorage.insert(tx), TransactionStatus::None);
    }
    BOOST_CHECK_EQUAL(limitedStorage.size(), kLimit);

    // Submitting a new tx with checkPoolLimit=true must be rejected before reaching validation
    auto tx4 = makeTx("fib55_n3", false);
    auto result =
        limitedStorage.verifyAndSubmitTransaction(tx4, nullptr, /*checkPoolLimit*/ true, false);
    BOOST_CHECK_EQUAL(result, TransactionStatus::TxPoolIsFull);
    BOOST_CHECK_EQUAL(limitedStorage.size(), kLimit);
}

BOOST_AUTO_TEST_CASE(FIB60_UnsealWithWrongBatchIdPreservesResealed)
{
    // FIB-60: When unsealing, the code moved txs to unsealTransactions regardless of whether
    // they had been re-sealed by a newer batch. The fix adds a re-seal guard: if a tx is sealed
    // and its batchId/batchHash doesn't match the unseal request, it is left sealed.
    auto tx0 = makeTx("fib60_n0", false);
    auto tx1 = makeTx("fib60_n1", false);
    auto tx2 = makeTx("fib60_n2", false);
    storage.insert(tx0);
    storage.insert(tx1);
    storage.insert(tx2);
    BOOST_CHECK_EQUAL(storage.size(), 3);

    // Seal all 3 txs with batch 1
    HashType batchHash1 = HashType::generateRandomFixedBytes();
    HashList batch1All{tx0->hash(), tx1->hash(), tx2->hash()};
    BOOST_CHECK(storage.batchMarkTxs(batch1All, 1, batchHash1, true));
    BOOST_CHECK(tx0->sealed());
    BOOST_CHECK(tx1->sealed());
    BOOST_CHECK(tx2->sealed());

    // Re-seal tx0 and tx1 with batch 2 (simulates a competing proposer)
    HashType batchHash2 = HashType::generateRandomFixedBytes();
    HashList batch2Partial{tx0->hash(), tx1->hash()};
    BOOST_CHECK(storage.batchMarkTxs(batch2Partial, 2, batchHash2, true));
    BOOST_CHECK_EQUAL(tx0->batchId(), 2);
    BOOST_CHECK_EQUAL(tx1->batchId(), 2);

    // Now unseal with the old batch 1 — tx0 and tx1 must be protected by the re-seal guard
    BOOST_CHECK(storage.batchMarkTxs(batch1All, 1, batchHash1, false));
    // tx0, tx1 were re-sealed to batch 2: must remain sealed
    BOOST_CHECK(tx0->sealed());
    BOOST_CHECK(tx1->sealed());
    // tx2 belonged to batch 1 and was not re-sealed: must be unsealed
    BOOST_CHECK(!tx2->sealed());
    // All 3 txs must still be present in the pool
    BOOST_CHECK_EQUAL(storage.size(), 3);
    BOOST_CHECK(storage.exists(tx0->hash()));
    BOOST_CHECK(storage.exists(tx1->hash()));
    BOOST_CHECK(storage.exists(tx2->hash()));
}

BOOST_AUTO_TEST_CASE(FIB48_AlreadyInTxPoolAndAcceptReturnsNone)
{
    // FIB-48: When a transaction without a callback is re-submitted with a callback,
    // txpoolStorageCheck() returns AlreadyInTxPoolAndAccept and sets the callback.
    // The old code continued through the lambda chain into insert(), causing a
    // use-after-free and potential double-resume of the coroutine handle.
    // The fix returns TransactionStatus::None immediately after registering the callback.

    auto tx1 = makeTx("fib48_n1", false);
    storage.insert(tx1);  // Insert without callback
    BOOST_CHECK_EQUAL(storage.size(), 1U);

    // Re-submit with a callback: tx exists, no prior callback → AlreadyInTxPoolAndAccept
    // Fix: returns None immediately (callback accepted) without re-entering insert()
    bool callbackCalled = false;
    auto result = storage.verifyAndSubmitTransaction(
        tx1,
        [&callbackCalled](
            Error::Ptr, protocol::TransactionSubmitResult::Ptr) { callbackCalled = true; },
        false, false);
    BOOST_CHECK(result == TransactionStatus::None);
    BOOST_CHECK_EQUAL(storage.size(), 1U);  // No duplicate insert

    // Re-submit again: tx now has a callback → AlreadyInTxPool (rejected outright)
    auto result2 = storage.verifyAndSubmitTransaction(tx1, nullptr, false, false);
    BOOST_CHECK(result2 == TransactionStatus::AlreadyInTxPool);
    BOOST_CHECK_EQUAL(storage.size(), 1U);
}

BOOST_AUTO_TEST_CASE(FIB48_SubmitTransactionResumesOnce)
{
    // Regression for double-resume risk in submitTransaction(waitForReceipt=true).
    // Scenario:
    // 1) tx already exists in pool without callback.
    // 2) submitTransaction(waitForReceipt=true) registers callback on existing tx
    //    through AlreadyInTxPoolAndAccept path.
    // 3) batchRemoveSealedTxs notifies result and resumes awaiting coroutine.
    // Expectation: coroutine continuation runs exactly once.

    // submitTransaction() uses shared_from_this(), so this instance must be owned by shared_ptr.
    auto sharedStorage = std::make_shared<MemoryStorage>(config);

    auto tx = makeTx("fib48_submit_once", false);
    BOOST_CHECK_EQUAL(sharedStorage->insert(tx), TransactionStatus::None);

    std::atomic<int> resumeCount{0};
    std::promise<void> donePromise;
    auto doneFuture = donePromise.get_future();

    std::thread waitThread([&]() {
        try
        {
            auto submitResult = task::syncWait(sharedStorage->submitTransaction(tx, true));
            BOOST_REQUIRE(submitResult);
            resumeCount.fetch_add(1, std::memory_order_relaxed);
            donePromise.set_value();
        }
        catch (...)
        {
            donePromise.set_exception(std::current_exception());
        }
    });

    // Wait until callback is attached, or submit coroutine has already completed.
    bool callbackAttached = false;
    for (size_t i = 0; i < 10000; ++i)
    {
        if (tx->submitCallback())
        {
            callbackAttached = true;
            break;
        }
        if (doneFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
        {
            break;
        }
        std::this_thread::yield();
    }

    TransactionSubmitResults txsResult;
    if (callbackAttached)
    {
        // Seal tx then notify execution result to trigger callback resume path.
        HashType batchHash = HashType::generateRandomFixedBytes();
        HashList txHashes{tx->hash()};
        BOOST_CHECK(
            sharedStorage->batchMarkTxs(txHashes, /*batchId*/ 1, batchHash, /*sealFlag*/ true));

        auto txResult = std::make_shared<TransactionSubmitResultImpl>();
        txResult->setTxHash(tx->hash());
        txResult->setStatus(static_cast<uint32_t>(TransactionStatus::None));
        txsResult.push_back(txResult);
        sharedStorage->batchRemoveSealedTxs(/*batchId*/ 1, txsResult);
    }

    BOOST_CHECK(doneFuture.wait_for(std::chrono::seconds(1)) == std::future_status::ready);
    if (waitThread.joinable())
    {
        waitThread.join();
    }
    BOOST_REQUIRE_NO_THROW(doneFuture.get());
    BOOST_REQUIRE(callbackAttached);

    // Triggering removal notification again should not re-run continuation.
    sharedStorage->batchRemoveSealedTxs(/*batchId*/ 1, txsResult);
    BOOST_CHECK_EQUAL(resumeCount.load(std::memory_order_relaxed), 1);
}

BOOST_AUTO_TEST_CASE(FIB50_NonceNotInsertedOnValidationFailure)
{
    // FIB-50: nonce must only be inserted AFTER all validation steps pass.
    // Old code called txPoolNonceChecker->insert() inside TxValidator::verify() — before
    // validateTransaction(). If validateTransaction later failed (e.g. OverFlowValue), the
    // nonce was already stuck in the pool, preventing valid re-submission.
    // Fix: nonce insertion is deferred to verifyAndSubmitTransaction(), after all steps pass.

    // Use a real TxPoolNonceChecker so we can query exists()
    auto realNC = std::make_shared<TxPoolNonceChecker>();
    std::shared_ptr<NonceCheckerInterface> nc = realNC;

    // Fresh validator mock with all required methods set up
    fakeit::Mock<bcos::txpool::TxValidatorInterface> localValidator;
    fakeit::Mock<bcos::txpool::LedgerNonceChecker> localLNC;
    auto web3Checker = std::make_shared<bcos::txpool::Web3NonceChecker>(nullptr);
    fakeit::When(Method(localValidator, web3NonceChecker)).AlwaysReturn(web3Checker);
    auto lnc = std::shared_ptr<bcos::txpool::LedgerNonceChecker>(&localLNC.get(), [](auto*) {});
    fakeit::When(Method(localValidator, ledgerNonceChecker)).AlwaysReturn(lnc);
    fakeit::When(Method(localLNC, batchInsert)).AlwaysDo([](auto, auto const&) {});

    // verify() always passes — bypasses real signature verification for test simplicity
    fakeit::When(Method(localValidator, verify)).AlwaysReturn(TransactionStatus::None);

    // validateTransaction(): reject the bad nonce, accept all others
    const std::string badNonce = "fib50_bad_nonce";
    fakeit::When(Method(localValidator, validateTransaction))
        .AlwaysDo([badNonce](const bcos::protocol::Transaction& tx) -> TransactionStatus {
            return std::string(tx.nonce()) == badNonce ? TransactionStatus::OverFlowValue :
                                                         TransactionStatus::None;
        });

    // validateChainId() always passes
    fakeit::When(Method(localValidator, validateChainId))
        .AlwaysDo([](const auto&, auto) -> task::Task<TransactionStatus> {
            co_return TransactionStatus::None;
        });

    std::shared_ptr<TxValidatorInterface> v(&localValidator.get(), [](auto*) {});
    auto cfg = std::make_shared<TxPoolConfig>(
        v, nullptr, nullptr, nullptr, nc, 1000, 1024, /*checkSig=*/true);
    MemoryStorage stor(cfg);

    // 1. Bad tx: validateTransaction returns OverFlowValue → chain stops → nonce NOT inserted
    auto badTx = makeTx(badNonce, false);
    auto r1 = stor.verifyAndSubmitTransaction(badTx, nullptr, false, false);
    BOOST_CHECK_EQUAL(r1, TransactionStatus::OverFlowValue);
    // FIB-50 fix: nonce was not inserted because validation failed before the insertion point
    BOOST_CHECK(!realNC->exists(badNonce));

    // 2. Good tx: all steps pass → nonce IS inserted and tx enters pool
    const std::string goodNonce = "fib50_good_nonce";
    auto goodTx = makeTx(goodNonce, false);
    auto r2 = stor.verifyAndSubmitTransaction(goodTx, nullptr, false, false);
    BOOST_CHECK_EQUAL(r2, TransactionStatus::None);
    BOOST_CHECK(realNC->exists(goodNonce));
    BOOST_CHECK_EQUAL(stor.size(), 1U);
}

BOOST_AUTO_TEST_CASE(FIB65_SealAtIndex0UpdatesKnownHash)
{
    // FIB-65: batchMarkTxs must update m_knownLatestSealedTxHash even when the sealed
    // transaction is at index 0 of the hash list. The old code used `> 0` which skipped
    // index 0, leaving m_knownLatestSealedTxHash stale and causing batchSealTransactions
    // to start from the wrong position on subsequent calls.

    // Provide a real BlockFactory so batchSealTransactions can create TransactionMetaData
    auto hashImpl = std::make_shared<Keccak256>();
    auto signatureImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, txFactory, receiptFactory);
    config->setBlockFactory(blockFactory);

    // Step 1: Insert tx1 (unsealed) and seal it via batchMarkTxs at index 0
    auto tx1 = makeTx("fib65_nonce1", false);
    tx1->setImportTime(static_cast<int64_t>(utcTime()));
    storage.insert(tx1);
    HashType batchHash = HashType::generateRandomFixedBytes();
    HashList toSeal{tx1->hash()};  // tx1 is at index 0 — this is the bug trigger
    bool ok = storage.batchMarkTxs(toSeal, /*batchId*/ 1, batchHash, /*sealFlag*/ true);
    BOOST_CHECK(ok);
    BOOST_CHECK(tx1->sealed());

    // Step 2: Insert tx2 (unsealed) — should be found by the next batchSealTransactions
    auto tx2 = makeTx("fib65_nonce2", false);
    tx2->setImportTime(static_cast<int64_t>(utcTime()));
    storage.insert(tx2);
    BOOST_CHECK(!tx2->sealed());

    // Step 3: batchSealTransactions must find tx2 regardless of m_knownLatestSealedTxHash
    std::vector<protocol::TransactionMetaData::Ptr> txsList;
    std::vector<protocol::TransactionMetaData::Ptr> sysTxsList;
    bool result = storage.batchSealTransactions(txsList, sysTxsList, /*limit*/ 100);
    BOOST_CHECK(result);

    // tx2 must be in the output (the fix ensures rangeByKey starts from the correct position)
    bool foundTx2 = false;
    for (auto& meta : txsList)
    {
        if (meta->hash() == tx2->hash())
        {
            foundTx2 = true;
            break;
        }
    }
    BOOST_CHECK(foundTx2);
    // Total: tx1 (sealed) + tx2 (now sealed after batchSealTransactions) = 2
    BOOST_CHECK_EQUAL(storage.size(), 2U);
}
BOOST_AUTO_TEST_CASE(FIB54_ConcurrentBatchMarkTxs)
{
    // FIB-54: batchMarkTxs used ReadAccessor inside the traverse callback but then
    // called bucket.remove() and batchInsert() which need write access, causing a data
    // race under concurrent sealing. The fix promotes to WriteAccessor for all mutations.
    // This test inserts 50 txs and concurrently seals two non-overlapping batches to
    // exercise the concurrent-write path.

    constexpr int kTotal = 50;
    constexpr int kBatch1End = 25;  // batch 1: indices 0..24, batch 2: indices 25..49

    std::vector<bcostars::protocol::TransactionImpl::Ptr> txs;
    txs.reserve(kTotal);
    for (int i = 0; i < kTotal; ++i)
    {
        auto tx = makeTx("fib54_n" + std::to_string(i), false);
        storage.insert(tx);
        txs.push_back(tx);
    }
    BOOST_CHECK_EQUAL(storage.size(), static_cast<std::size_t>(kTotal));

    HashType bh1 = HashType::generateRandomFixedBytes();
    HashType bh2 = HashType::generateRandomFixedBytes();

    HashList batch1;
    HashList batch2;
    for (int i = 0; i < kBatch1End; ++i)
    {
        batch1.push_back(txs[i]->hash());
    }
    for (int i = kBatch1End; i < kTotal; ++i)
    {
        batch2.push_back(txs[i]->hash());
    }

    // Seal both batches concurrently — must not crash or corrupt state (FIB-54)
    tbb::parallel_invoke([&] { storage.batchMarkTxs(batch1, /*batchId=*/10, bh1, /*seal=*/true); },
        [&] { storage.batchMarkTxs(batch2, /*batchId=*/11, bh2, /*seal=*/true); });

    // All 50 txs must now be sealed
    for (auto& tx : txs)
    {
        BOOST_CHECK(tx->sealed());
    }
    BOOST_CHECK_EQUAL(storage.size(), static_cast<std::size_t>(kTotal));

    // Unseal both batches concurrently
    tbb::parallel_invoke([&] { storage.batchMarkTxs(batch1, /*batchId=*/10, bh1, /*seal=*/false); },
        [&] { storage.batchMarkTxs(batch2, /*batchId=*/11, bh2, /*seal=*/false); });

    // All 50 txs must now be unsealed and still present
    for (auto& tx : txs)
    {
        BOOST_CHECK(!tx->sealed());
        BOOST_CHECK(storage.exists(tx->hash()));
    }
    BOOST_CHECK_EQUAL(storage.size(), static_cast<std::size_t>(kTotal));
}

BOOST_AUTO_TEST_SUITE_END()
