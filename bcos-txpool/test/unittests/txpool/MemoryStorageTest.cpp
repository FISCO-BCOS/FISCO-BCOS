/**
 *  Copyright (C) 2025.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/txpool/Constant.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/Error.h"
#include "bcos-utilities/IOServicePool.h"
#include <bcos-tx-validator/LedgerNonceChecker.h>
#include <bcos-tx-validator/NonceCheckerInterface.h>
#include <bcos-tx-validator/TxPoolNonceChecker.h>
#include <bcos-tx-validator/Web3NonceChecker.h>

#include <tbb/parallel_invoke.h>

#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <fakeit.hpp>
#include <future>
#include <thread>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::protocol;
using namespace bcos::crypto;

struct MemoryStorageFixture
{
    MemoryStorageFixture()
      : txValidator(&mockValidator.get(), [](bcos::txpool::TxValidatorInterface*) {}),
        txPoolNonceChecker(
            &mockNonceChecker.get(), [](bcos::txvalidator::NonceCheckerInterface*) {}),
        ledgerNonceChecker(
            &mockLedgerNonceChecker.get(), [](bcos::txvalidator::LedgerNonceChecker*) {}),
        ledger(&mockLedger.get(), [](bcos::ledger::LedgerInterface*) {}),
        config(std::make_shared<TxPoolConfig>(txValidator,
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>(), nullptr,
            nullptr, txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ 1024,
            /*checkSig*/ false)),
        storage(config, *ioServicePool->getIOService())
    {
        fakeit::When(Method(mockValidator, checkTransaction))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);

        // txvalidator::Web3NonceChecker: return a usable instance (internal structures are
        // in-memory only; pass nullptr for ledger)
        auto web3Checker = std::make_shared<bcos::txvalidator::Web3NonceChecker>(nullptr);
        fakeit::When(Method(mockValidator, web3NonceChecker)).AlwaysReturn(web3Checker);

        // txvalidator::LedgerNonceChecker: set all methods to no-op implementations
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
        // No calculateHash() here: this fabricated tx has no signing preimage/signature, and
        // since FIB-New1 the Web3 branch of calculateHash() unconditionally recomputes the
        // canonical hash from them (throwing on absence). hash() reads the value set above.
        return tx;
    }

    void setLegacySigningPreimage(bcostars::protocol::TransactionImpl& tx, uint64_t envelopeChainId)
    {
        namespace rlp = bcos::codec::rlp;
        bytes items;
        rlp::encode(items, uint64_t{0});      // nonce
        rlp::encode(items, uint64_t{0});      // gasPrice
        rlp::encode(items, uint64_t{21000});  // gasLimit
        rlp::encode(items, bytes{});          // to
        rlp::encode(items, uint64_t{0});      // value
        rlp::encode(items, bytes{});          // data
        rlp::encode(items, envelopeChainId);
        rlp::encode(items, uint64_t{0});
        rlp::encode(items, uint64_t{0});

        bytes envelope;
        rlp::encodeHeader(envelope, rlp::Header{.isList = true, .payloadLength = items.size()});
        envelope.insert(envelope.end(), items.begin(), items.end());
        tx.mutableInner().extraTransactionBytes.assign(envelope.begin(), envelope.end());
    }

    void setTypedChainIdEnvelope(
        bcostars::protocol::TransactionImpl& tx, uint8_t type, uint64_t envelopeChainId)
    {
        namespace rlp = bcos::codec::rlp;
        bytes fields;
        rlp::encode(fields, envelopeChainId);
        bytes envelope;
        envelope.push_back(type);
        rlp::encodeHeader(envelope, rlp::Header{.isList = true, .payloadLength = fields.size()});
        envelope.insert(envelope.end(), fields.begin(), fields.end());
        tx.mutableInner().extraTransactionBytes.assign(envelope.begin(), envelope.end());
    }

    // pre-EIP-155 6-field signing preimage: classifier Unprotected, no chainId binding.
    void setLegacyUnprotectedPreimage(bcostars::protocol::TransactionImpl& tx)
    {
        namespace rlp = bcos::codec::rlp;
        bytes items;
        rlp::encode(items, uint64_t{0});      // nonce
        rlp::encode(items, uint64_t{0});      // gasPrice
        rlp::encode(items, uint64_t{21000});  // gasLimit
        rlp::encode(items, bytes{});          // to
        rlp::encode(items, uint64_t{0});      // value
        rlp::encode(items, bytes{});          // data

        bytes envelope;
        rlp::encodeHeader(envelope, rlp::Header{.isList = true, .payloadLength = items.size()});
        envelope.insert(envelope.end(), items.begin(), items.end());
        tx.mutableInner().extraTransactionBytes.assign(envelope.begin(), envelope.end());
    }

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txvalidator::NonceCheckerInterface> mockNonceChecker;
    fakeit::Mock<bcos::txvalidator::LedgerNonceChecker> mockLedgerNonceChecker;
    fakeit::Mock<bcos::ledger::LedgerInterface> mockLedger;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txvalidator::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<bcos::txvalidator::LedgerNonceChecker> ledgerNonceChecker;
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger;
    std::shared_ptr<TxPoolConfig> config;
    bcos::IOServicePool::Ptr ioServicePool =
        std::make_shared<bcos::IOServicePool>(1, "memStorTest");
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

BOOST_AUTO_TEST_CASE(BatchRemoveSealedTxsUpdatesWeb3NonceCache)
{
    // This test verifies that batchRemoveSealedTxs correctly updates the Web3 nonce cache
    // when sealed Web3 transactions are removed.

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

    // The key part of the test: verify that txvalidator::Web3NonceChecker was updated with correct
    // data. The web3NonceChecker should have been updated with:
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

    fakeit::Mock<bcos::txvalidator::Web3NonceChecker> mockWeb3NonceChecker;
    fakeit::When(Method(mockWeb3NonceChecker, insertMemoryNonce))
        .AlwaysDo([](auto, auto) -> task::Task<bool> { co_return true; });

    std::shared_ptr<bcos::txvalidator::Web3NonceChecker> web3NonceChecker(
        &mockWeb3NonceChecker.get(), [](bcos::txvalidator::Web3NonceChecker*) {});
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
    MemoryStorage storageWithSig(configWithSig, *ioServicePool->getIOService());

    // Create config with signature check disabled
    auto configNoSig = std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, ledger,
        txPoolNonceChecker, /*blockLimit*/ 1000,
        /*poolLimit*/ 1024, /*checkSig*/ false);
    MemoryStorage storageNoSig(configNoSig, *ioServicePool->getIOService());

    // Test 1: Step 1 - AlreadyInTxPool
    {
        auto tx1 = makeTx("nonce1", false);
        storageWithSig.insert(tx1);  // Insert first time
        auto result = storageWithSig.verifyAndSubmitTransaction(tx1, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::AlreadyInTxPool);
    }

    // Test 5: Step 3 - MaxInitCodeSizeExceeded (for Web3Transaction)
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx5 = makeWeb3Tx("0x1", senderHex, false);
        // Set input size larger than MAX_INITCODE_SIZE - need to cast to TransactionImpl
        auto tx5Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx5);
        BOOST_REQUIRE(tx5Impl);  // finding BK: a cast-shape change must fail the test, not silently
                                 // skip the seeding
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
        BOOST_REQUIRE(tx6Impl);  // finding BK: a cast-shape change must fail the test, not silently
                                 // skip the seeding
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
        BOOST_REQUIRE(tx7Impl);  // finding BK: a cast-shape change must fail the test, not silently
                                 // skip the seeding
        {
            setLegacySigningPreimage(*tx7Impl, 123);
            // The forgeable mirror deliberately matches the node. Admission must still reject
            // the signed envelope's chainId rather than trusting this field.
            tx7Impl->mutableInner().data.chainID = "321";
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
        auto tx8Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx8);
        BOOST_REQUIRE(tx8Impl);  // finding BK: a cast-shape change must fail the test, not silently
                                 // skip the seeding
        {
            std::string smallValue = "0x100";
            tx8Impl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            // Well-formed EIP-155 preimage matching mocked web3_chain_id; tars chainID left empty.
            setLegacySigningPreimage(*tx8Impl, 321);
            tx8Impl->mutableInner().data.chainID = "";
        }

        // Mock ledger to return nullptr (simplified test)
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);

        // Setup validator to pass all checks
        auto ledgerNonceChecker = std::make_shared<txvalidator::LedgerNonceChecker>(
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
        BOOST_REQUIRE(tx9Impl);  // finding BK: a cast-shape change must fail the test, not silently
                                 // skip the seeding
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
        BOOST_REQUIRE(tx10Impl);  // finding BK: a cast-shape change must fail the test, not
                                  // silently skip the seeding
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

    // Test 11: the EIP-2 low-s hook in TxValidator::verify is the ONLY low-s
    // enforcement a P2P-synced tars-form Web3 tx ever meets — the raw-bytes decode funnel,
    // which rejects high-s inline, is not on this path. Sign genuinely, then flip s to
    // n-s: recovery still SUCCEEDS (deriving an unrelated address — a same-sender replay
    // would need v flipped too), so without the hook this tx would enter the pool under a
    // distinct txHash for the same logical transaction. The low-s
    // baseline is the positive control (deleting the hook flips the second check green).
    {
        fakeit::When(Method(mockLedger, asyncGetBlockNumber)).AlwaysDo([](auto) -> long long {
            return 0;
        });
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);
        // Finding N7: T19-style full stubs so the exempt tx walks the whole post-gate path
        // and the assertion pins the healthy outcome instead of "any non-chainId status".
        fakeit::When(Method(mockLedger, asyncGetSystemConfigByKey))
            .AlwaysDo(
                [](auto const& key,
                    std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                    if (key == ledger::SYSTEM_KEY_TX_GAS_PRICE)
                    {
                        callback(nullptr, "0", 0);
                    }
                    else
                    {
                        // Same values as the case's earlier webhook ("321"): this stub must
                        // not change outcomes for the later protected-chain sections in
                        // this test case (fakeit stubs persist for the whole case).
                        callback(nullptr, "321", 0);
                    }
                });
        auto ledgerNonceChecker = std::make_shared<txvalidator::LedgerNonceChecker>(
            nullptr, /*blockNumber*/ 0, /*blockLimit*/ 1000, /*checkBlockLimit*/ false);
        txValidator->setLedgerNonceChecker(ledgerNonceChecker);

        bcos::bytes preimage;
        bcos::codec::rlp::encode(preimage, static_cast<uint64_t>(9), static_cast<uint64_t>(1),
            static_cast<uint64_t>(21000), bcos::bytes{}, static_cast<uint64_t>(0),
            std::string("high-s probe"));
        auto const sigHash = bcos::crypto::keccak256Hash(bcos::ref(preimage));
        auto const signedLowS = signatureImpl->sign(*keyPair, sigHash, true);

        auto makeSignedWeb3Tx = [&](bcos::bytes signature) {
            auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
            tx->setNonce("0x11");
            tx->mutableInner().type =
                static_cast<uint8_t>(bcos::protocol::TransactionType::Web3Transaction);
            tx->setSignatureData(signature);
            tx->mutableInner().extraTransactionBytes.assign(preimage.begin(), preimage.end());
            tx->calculateHash(*hashImpl);
            return tx;
        };

        // Positive control: the canonical low-s signature passes the full chain.
        bcos::bytes lowS(signedLowS->begin(), signedLowS->end());
        auto baseline = makeSignedWeb3Tx(lowS);
        auto const baselineResult =
            storageWithSig.verifyAndSubmitTransaction(baseline, nullptr, false, false);
        BOOST_CHECK(baselineResult == TransactionStatus::None);

        // High-s twin: s' = n - s (secp256k1 group order).
        bcos::u256 const curveN(
            "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
        bcos::u256 const sVal =
            bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef(lowS.data() + 32, 32));
        bcos::u256 const flipped = curveN - sVal;
        bcos::bytes highS = lowS;
        for (std::size_t i = 0; i < 32; ++i)
        {
            auto const shift = (31 - i) * 8;
            highS[32 + i] =
                static_cast<bcos::byte>(((flipped >> shift) & 0xff).convert_to<unsigned int>());
        }
        auto tampered = makeSignedWeb3Tx(highS);
        auto const result =
            storageWithSig.verifyAndSubmitTransaction(tampered, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::InvalidSignature);
    }

    // Test 12: empty extraTransactionBytes is dispatch Unsupported → Malformed.
    // (Classifier Malformed used to map this to InvalidChainId; the P2P funnel must not
    // treat an untyped/empty payload as a chainId question.)
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx12 = makeWeb3Tx("0x12", senderHex, false);
        auto const result = storageNoSig.verifyAndSubmitTransaction(tx12, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::Malformed);
    }

    auto mockMissingWeb3ChainId = [&]() {
        fakeit::When(Method(mockLedger, asyncGetSystemConfigByKey))
            .AlwaysDo(
                [](auto const& key,
                    std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                    if (key == ledger::SYSTEM_KEY_WEB3_CHAIN_ID)
                    {
                        callback(BCOS_ERROR_PTR(
                                     ledger::LedgerError::EmptyEntry, "missing web3_chain_id"),
                            "", 0);
                    }
                    else if (key == ledger::SYSTEM_KEY_TX_GAS_PRICE)
                    {
                        callback(nullptr, "0", 0);
                    }
                });
    };

    // Matrix: T05 — missing web3_chain_id rejects Protected EIP-155, exempts Unprotected.
    {
        storageNoSig.clear();
        mockMissingWeb3ChainId();
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);
        auto ledgerNonceChecker = std::make_shared<txvalidator::LedgerNonceChecker>(
            nullptr, /*blockNumber*/ 0, /*blockLimit*/ 1000, /*checkBlockLimit*/ false);
        txValidator->setLedgerNonceChecker(ledgerNonceChecker);

        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto protectedTx = makeWeb3Tx("0x15", senderHex, false);
        auto protectedImpl =
            std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(protectedTx);
        BOOST_REQUIRE(protectedImpl);
        {
            std::string smallValue = "0x100";
            protectedImpl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            setLegacySigningPreimage(*protectedImpl, 123);
            protectedImpl->mutableInner().data.chainID = "";
        }
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(protectedTx, nullptr, false, false) ==
                    TransactionStatus::InvalidChainId);

        auto unprotectedTx = makeWeb3Tx("0x16", senderHex, false);
        auto unprotectedImpl =
            std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(unprotectedTx);
        BOOST_REQUIRE(unprotectedImpl);
        {
            std::string smallValue = "0x100";
            unprotectedImpl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            setLegacyUnprotectedPreimage(*unprotectedImpl);
            unprotectedImpl->mutableInner().data.chainID = "";
        }
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(unprotectedTx, nullptr, false, false) ==
                    TransactionStatus::None);
    }

    // Matrix: T19 — hex QUANTITY web3_chain_id ("0x539" == 1337) admits a matching envelope.
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx19 = makeWeb3Tx("0x19", senderHex, false);
        auto tx19Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx19);
        BOOST_REQUIRE(tx19Impl);
        {
            std::string smallValue = "0x100";
            tx19Impl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            setLegacySigningPreimage(*tx19Impl, 1337);
            tx19Impl->mutableInner().data.chainID = "";
        }
        fakeit::When(Method(mockLedger, asyncGetSystemConfigByKey))
            .AlwaysDo(
                [](auto const& key,
                    std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> callback) {
                    if (key == ledger::SYSTEM_KEY_WEB3_CHAIN_ID)
                    {
                        callback(nullptr, "0x539", 0);
                    }
                    else if (key == ledger::SYSTEM_KEY_TX_GAS_PRICE)
                    {
                        callback(nullptr, "0", 0);
                    }
                });
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);
        auto ledgerNonceChecker = std::make_shared<txvalidator::LedgerNonceChecker>(
            nullptr, /*blockNumber*/ 0, /*blockLimit*/ 1000, /*checkBlockLimit*/ false);
        txValidator->setLedgerNonceChecker(ledgerNonceChecker);
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(tx19, nullptr, false, false) ==
                    TransactionStatus::None);
    }

    // Matrix: T20 — extraTxBytes starting 0x03 (blob) is rejected at the pool gate.
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx20 = makeWeb3Tx("0x20", senderHex, false);
        auto tx20Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx20);
        BOOST_REQUIRE(tx20Impl);
        {
            bcos::bytes blobPrefix{0x03};
            tx20Impl->mutableInner().extraTransactionBytes.assign(
                blobPrefix.begin(), blobPrefix.end());
        }
        // Finding BO: the blob gate now returns the dedicated BlobTxNotAllowed instead
        // of the InvalidChainId shared with the Deposit/Malformed/mismatch arms.
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(tx20, nullptr, false, false) ==
                    TransactionStatus::BlobTxNotAllowed);
    }

    // Matrix: T21 — extraTxBytes starting 0x7E (deposit) is rejected at the pool gate.
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx21 = makeWeb3Tx("0x21", senderHex, false);
        auto tx21Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx21);
        BOOST_REQUIRE(tx21Impl);
        {
            bcos::bytes depositPrefix{0x7e};
            tx21Impl->mutableInner().extraTransactionBytes.assign(
                depositPrefix.begin(), depositPrefix.end());
        }
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(tx21, nullptr, false, false) ==
                    TransactionStatus::InvalidChainId);
    }

    auto mockWeb3ChainId321 = [&]() {
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
    };

    // Matrix: T22 — extraTxBytes starting 0x05 (unsupported typed) is Malformed.
    {
        storageNoSig.clear();
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        auto tx22 = makeWeb3Tx("0x22", senderHex, false);
        auto tx22Impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx22);
        BOOST_REQUIRE(tx22Impl);
        {
            bcos::bytes unsupported{0x05};
            tx22Impl->mutableInner().extraTransactionBytes.assign(
                unsupported.begin(), unsupported.end());
        }
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(tx22, nullptr, false, false) ==
                    TransactionStatus::Malformed);
    }

    // Matrix: T23 — typed 0x01/0x02/0x04 admit when field0 matches; mismatch is InvalidChainId.
    {
        mockWeb3ChainId321();
        fakeit::When(Method(mockLedger, getStateStorage)).AlwaysReturn(nullptr);
        auto ledgerNonceChecker = std::make_shared<txvalidator::LedgerNonceChecker>(
            nullptr, /*blockNumber*/ 0, /*blockLimit*/ 1000, /*checkBlockLimit*/ false);
        txValidator->setLedgerNonceChecker(ledgerNonceChecker);
        const std::string senderHex = "0x1234567890123456789012345678901234567890";
        for (uint8_t const type : {uint8_t{0x01}, uint8_t{0x02}, uint8_t{0x04}})
        {
            storageNoSig.clear();
            auto tx =
                makeWeb3Tx(std::string("0x23") + static_cast<char>('0' + type), senderHex, false);
            auto txImpl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
            BOOST_REQUIRE(txImpl);
            {
                std::string smallValue = "0x100";
                txImpl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
                setTypedChainIdEnvelope(*txImpl, type, 321);
                txImpl->mutableInner().data.chainID = "";
            }
            BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(tx, nullptr, false, false) ==
                        TransactionStatus::None);
        }
        storageNoSig.clear();
        auto mismatch = makeWeb3Tx("0x24", senderHex, false);
        auto mismatchImpl =
            std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(mismatch);
        BOOST_REQUIRE(mismatchImpl);
        {
            std::string smallValue = "0x100";
            mismatchImpl->mutableInner().data.value.assign(smallValue.begin(), smallValue.end());
            setTypedChainIdEnvelope(*mismatchImpl, 0x02, 999);
            mismatchImpl->mutableInner().data.chainID = "";
        }
        BOOST_CHECK(storageNoSig.verifyAndSubmitTransaction(mismatch, nullptr, false, false) ==
                    TransactionStatus::InvalidChainId);
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
    // FIB-51: txvalidator::TxPoolNonceChecker::insert() now returns bool (true = newly inserted,
    // false = already existed). This makes the check-and-reserve atomic per bucket,
    // eliminating the TOCTOU window between separate checkNonce() + insert() calls.

    txvalidator::TxPoolNonceChecker checker;

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
    MemoryStorage limitedStorage(limitedConfig, *ioServicePool->getIOService());

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
    auto ioServicePool = std::make_shared<IOServicePool>(1, "memStorageTest");
    auto sharedStorage = std::make_shared<MemoryStorage>(config, *ioServicePool->getIOService());

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
    for (size_t i = 0; i < 200; ++i)
    {
        if (tx->submitCallback())
        {
            callbackAttached = true;
            break;
        }
        if (doneFuture.wait_for(std::chrono::milliseconds(10)) == std::future_status::ready)
        {
            break;
        }
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

    BOOST_CHECK_MESSAGE(doneFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
        "submitTransaction did not complete within 5 seconds");
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

    // Use a real txvalidator::TxPoolNonceChecker so we can query exists()
    auto realNC = std::make_shared<txvalidator::TxPoolNonceChecker>();
    std::shared_ptr<txvalidator::NonceCheckerInterface> nc = realNC;

    // Fresh validator mock with all required methods set up
    fakeit::Mock<bcos::txpool::TxValidatorInterface> localValidator;
    fakeit::Mock<bcos::txvalidator::LedgerNonceChecker> localLNC;
    auto web3Checker = std::make_shared<bcos::txvalidator::Web3NonceChecker>(nullptr);
    fakeit::When(Method(localValidator, web3NonceChecker)).AlwaysReturn(web3Checker);
    auto lnc =
        std::shared_ptr<bcos::txvalidator::LedgerNonceChecker>(&localLNC.get(), [](auto*) {});
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
    MemoryStorage stor(cfg, *ioServicePool->getIOService());

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
