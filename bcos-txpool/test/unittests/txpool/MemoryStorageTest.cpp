/**
 *  Copyright (C) 2025.
 *  SPDX-License-Identifier: Apache-2.0
 */
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/testutils/faker/FakeTransaction.h"
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
#include "bcos-txpool/txpool/validator/AdmissionAdapters.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/TxPoolNonceChecker.h"
#include "bcos-txpool/txpool/validator/TxValidator.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "bcos-utilities/IOServicePool.h"

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

namespace
{
/// MemoryStorage now routes admission through bcos-tx-validator, so every TxPoolConfig built in
/// this file needs one wired -- the production path does it in TxPoolFactory. Readers return
/// "account absent", which is what these storage-level tests want: they are about insertion,
/// dedup and pool limits, not about account state.
void wireAdmission(std::shared_ptr<TxPoolConfig> const& config,
    std::shared_ptr<bcos::ledger::LedgerInterface> const& ledger,
    bcos::crypto::CryptoSuite::Ptr const& cryptoSuite,
    bcos::txpool::NonceCheckerInterface::Ptr const& nonceChecker,
    bcos::txpool::TxValidatorInterface::Ptr const& validator,
    std::optional<bcos::txvalidator::PoolNonceQuery> poolQuery = std::nullopt,
    std::string groupId = "", std::string chainId = "")
{
    auto admission = std::make_shared<bcos::txvalidator::TxValidator>(
        cryptoSuite, ledger,
        []() -> task::Task<bcos::ledger::LedgerConfig::Ptr> {
            co_return std::make_shared<bcos::ledger::LedgerConfig>();
        },
        [](std::string_view) -> task::Task<std::optional<bcos::txvalidator::AccountState>> {
            co_return std::nullopt;
        },
        [](std::string_view) -> task::Task<std::optional<u256>> { co_return std::nullopt; },
        [](Transaction const&) { return false; }, std::move(groupId), std::move(chainId));
    config->setAdmission(std::move(admission),
        poolQuery.value_or(bcos::txpool::makePoolNonceQuery(nonceChecker, validator)));
}
}  // namespace

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
        storage(config, *ioServicePool->getIOService())
    {
        fakeit::When(Method(mockValidator, checkTransaction))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);

        auto cryptoSuite =
            std::make_shared<bcos::crypto::CryptoSuite>(std::make_shared<bcos::crypto::Keccak256>(),
                std::make_shared<bcos::crypto::Secp256k1Crypto>(), nullptr);
        m_cryptoSuite = cryptoSuite;
        wireAdmission(config, ledger, cryptoSuite, txPoolNonceChecker, txValidator);

        // Web3NonceChecker: return a usable instance (internal structures are in-memory only; pass
        // nullptr for ledger)
        auto web3Checker = std::make_shared<bcos::txpool::Web3NonceChecker>(nullptr);
        fakeit::When(Method(mockValidator, web3NonceChecker)).AlwaysReturn(web3Checker);

        // LedgerNonceChecker: set all methods to no-op implementations
        fakeit::When(Method(mockValidator, ledgerNonceChecker)).AlwaysReturn(ledgerNonceChecker);
        fakeit::When(Method(mockLedgerNonceChecker, batchInsert)).AlwaysDo([](auto, auto const&) {
        });
        // Reached directly now: admission's BcosPoolNonce check calls into this checker, where
        // the old code stopped at the TxValidatorInterface::checkTransaction mock above.
        fakeit::When(Method(mockLedgerNonceChecker, checkNonce))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);

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

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txpool::NonceCheckerInterface> mockNonceChecker;
    fakeit::Mock<bcos::txpool::LedgerNonceChecker> mockLedgerNonceChecker;
    fakeit::Mock<bcos::ledger::LedgerInterface> mockLedger;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txpool::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<bcos::txpool::LedgerNonceChecker> ledgerNonceChecker;
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger;
    std::shared_ptr<TxPoolConfig> config;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
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

    // The key part of the test: verify that Web3NonceChecker was updated with correct data.
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
    // The per-check assertions that used to live here (initcode size, balance, chainId) moved to
    // bcos-tx-validator's AdmitTest, where the transactions are genuinely signed. They cannot be
    // expressed here any more, and that is the point of the change: makeWeb3Tx fabricates a
    // Web3 transaction with NO signed envelope and a random extraTransactionHash, and such a
    // transaction is now refused at normalization instead of being carried far enough to fail a
    // later check. What remains here is what belongs to storage: dedup, pool limit, and that
    // admission is actually consulted.
    auto configNoSig = std::make_shared<TxPoolConfig>(txValidator, nullptr, nullptr, ledger,
        txPoolNonceChecker, /*blockLimit*/ 1000,
        /*poolLimit*/ 1024, /*checkSig*/ false);
    wireAdmission(configNoSig, ledger, m_cryptoSuite, txPoolNonceChecker, txValidator);
    MemoryStorage storageNoSig(configNoSig, *ioServicePool->getIOService());

    // Step 1: a transaction already in the pool is reported as such, before any validation.
    {
        auto tx1 = makeTx("nonce1", false);
        storageNoSig.insert(tx1);
        auto result = storageNoSig.verifyAndSubmitTransaction(tx1, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::AlreadyInTxPool);
    }

    // A Web3 transaction whose tars mirror is not backed by a signed envelope is refused. Before
    // this change it was admitted: nothing compared the mirror against extraTransactionBytes, so
    // a peer could fabricate one outright.
    {
        storageNoSig.clear();
        auto forged = makeWeb3Tx("0x1", "0x1234567890123456789012345678901234567890", false);
        auto result = storageNoSig.verifyAndSubmitTransaction(forged, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::Malformed);
        BOOST_CHECK_EQUAL(storageNoSig.size(), 0U);
    }

    // A BCOS transaction still goes through: its dataHash covers the whole TransactionData, so
    // normalization leaves it alone.
    {
        storageNoSig.clear();
        auto bcosTx = makeTx("bcos_ok_nonce", false);
        auto result = storageNoSig.verifyAndSubmitTransaction(bcosTx, nullptr, false, false);
        BOOST_CHECK(result == TransactionStatus::None);
        BOOST_CHECK_EQUAL(storageNoSig.size(), 1U);
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
    wireAdmission(limitedConfig, ledger, m_cryptoSuite, txPoolNonceChecker, txValidator);
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
    // FIB-50: a transaction that fails validation must not leave its nonce reserved in the pool,
    // or that nonce is burned for the sender until restart. Insertion happens after the whole
    // admission call, so any admission failure has to leave the checker untouched.
    //
    // The failure is injected through PoolNonceQuery, which backs the BcosPoolNonce check -- the
    // LAST entry in c_checkOrder. A nonce that survives a rejection there survives one anywhere.
    //
    // checkSig stays enabled because the nonce-reservation block is gated on it, so the
    // transactions here are genuinely signed (admission now really verifies them; the previous
    // version of this test relied on a mocked verify()).
    auto realNC = std::make_shared<bcos::txpool::TxPoolNonceChecker>();
    const std::string badNonce = "fib50_bad_nonce";

    auto cfg = std::make_shared<TxPoolConfig>(
        txValidator, nullptr, nullptr, ledger, realNC, 1000, 1024, /*checkSig=*/true);
    wireAdmission(cfg, ledger, m_cryptoSuite, realNC, txValidator,
        bcos::txvalidator::PoolNonceQuery{.checkBcosNonce =
                                              [badNonce](Transaction const& tx, bool) {
                                                  return std::string(tx.nonce()) == badNonce ?
                                                             TransactionStatus::OverFlowValue :
                                                             TransactionStatus::None;
                                              }},
        "groupId", "chainId");
    MemoryStorage stor(cfg, *ioServicePool->getIOService());

    auto badTx =
        bcos::test::fakeTransaction(m_cryptoSuite, badNonce, 1000023, "chainId", "groupId");
    auto r1 = stor.verifyAndSubmitTransaction(badTx, nullptr, false, false);
    BOOST_CHECK_EQUAL(r1, TransactionStatus::OverFlowValue);
    BOOST_CHECK(!realNC->exists(badNonce));

    const std::string goodNonce = "fib50_good_nonce";
    auto goodTx =
        bcos::test::fakeTransaction(m_cryptoSuite, goodNonce, 1000023, "chainId", "groupId");
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
