/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Regression test: the consensus module must learn the unsealed-tx count synchronously
 *        at every transition of the pool, not from a periodic sample. With sampling,
 *        PBFTConfig::freshTimer() at block finalize could re-arm the view-change timer on a
 *        count taken while the just-committed txs were still in the pool, and a low-traffic
 *        chain then view-changed 3s after every block, skipping the next leader.
 * @file UnsealedTxsSizeNotifyTest.cpp
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-framework/protocol/TransactionMetaData.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-txpool/TxPoolConfig.h"
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/interfaces/TxValidatorInterface.h"
#include "bcos-txpool/txpool/storage/MemoryStorage.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include "bcos-utilities/Common.h"
#include <tbb/concurrent_unordered_set.h>
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <memory>
#include <vector>

namespace bcos::test
{
// Exposes the protected notifier entry so a test can stand in for the periodic backstop tick.
class UnsealedNotifyProbeStorage : public bcos::txpool::MemoryStorage
{
public:
    using MemoryStorage::MemoryStorage;
    using MemoryStorage::notifyUnsealedTxsSize;
};

struct UnsealedTxsSizeNotifyFixture
{
    UnsealedTxsSizeNotifyFixture()
      : txValidator(&mockValidator.get(), [](bcos::txpool::TxValidatorInterface*) {}),
        txPoolNonceChecker(&mockNonceChecker.get(), [](bcos::txpool::NonceCheckerInterface*) {}),
        ledgerNonceChecker(&mockLedgerNonceChecker.get(), [](bcos::txpool::LedgerNonceChecker*) {}),
        config(std::make_shared<bcos::txpool::TxPoolConfig>(txValidator,
            std::make_shared<bcos::protocol::TransactionSubmitResultFactoryImpl>(), nullptr,
            nullptr, txPoolNonceChecker, /*blockLimit*/ 0, /*poolLimit*/ 1024,
            /*checkSig*/ false)),
        storage(std::make_shared<UnsealedNotifyProbeStorage>(config))
    {
        fakeit::When(Method(mockValidator, checkTransaction))
            .AlwaysReturn(bcos::protocol::TransactionStatus::None);
        auto web3Checker = std::make_shared<bcos::txpool::Web3NonceChecker>(nullptr);
        fakeit::When(Method(mockValidator, web3NonceChecker)).AlwaysReturn(web3Checker);
        fakeit::When(Method(mockValidator, ledgerNonceChecker)).AlwaysReturn(ledgerNonceChecker);
        fakeit::When(Method(mockLedgerNonceChecker, batchInsert)).AlwaysDo([](auto, auto const&) {
        });
        fakeit::When(
            OverloadedMethod(mockNonceChecker, batchRemove, void(bcos::protocol::NonceList const&)))
            .AlwaysDo([](auto const&) {});
        fakeit::When(OverloadedMethod(mockNonceChecker, batchRemove,
                         void(tbb::concurrent_unordered_set<bcos::protocol::NonceType,
                             std::hash<bcos::protocol::NonceType>> const&)))
            .AlwaysDo([](auto const&) {});

        // Stand-in for PBFTImpl::asyncNotifyTxsSize: record every delivery in order.
        storage->registerTxsNotifier(
            [this](size_t txsSize, std::function<void(bcos::Error::Ptr)> callback) {
                delivered.push_back(txsSize);
                if (callback)
                {
                    callback(nullptr);
                }
            });
    }

    bcostars::protocol::TransactionImpl::Ptr makeTx(std::string nonce, bool sealed)
    {
        auto tx = std::make_shared<bcostars::protocol::TransactionImpl>();
        tx->setNonce(std::move(nonce));
        tx->setSealed(sealed);
        bcos::crypto::Keccak256 keccak;
        tx->calculateHash(keccak);
        return tx;
    }

    static bcos::protocol::TransactionSubmitResults committedResult(
        bcos::protocol::Transaction const& tx)
    {
        auto result = std::make_shared<bcos::protocol::TransactionSubmitResultImpl>();
        result->setTxHash(tx.hash());
        result->setStatus(static_cast<uint32_t>(bcos::protocol::TransactionStatus::None));
        result->setNonce(std::string(tx.nonce()));
        return {result};
    }

    size_t last() const { return delivered.empty() ? SIZE_MAX : delivered.back(); }

    fakeit::Mock<bcos::txpool::TxValidatorInterface> mockValidator;
    fakeit::Mock<bcos::txpool::NonceCheckerInterface> mockNonceChecker;
    fakeit::Mock<bcos::txpool::LedgerNonceChecker> mockLedgerNonceChecker;
    std::shared_ptr<bcos::txpool::TxValidatorInterface> txValidator;
    std::shared_ptr<bcos::txpool::NonceCheckerInterface> txPoolNonceChecker;
    std::shared_ptr<bcos::txpool::LedgerNonceChecker> ledgerNonceChecker;
    std::shared_ptr<bcos::txpool::TxPoolConfig> config;
    std::shared_ptr<UnsealedNotifyProbeStorage> storage;
    std::vector<size_t> delivered;
};

BOOST_FIXTURE_TEST_SUITE(UnsealedTxsSizeNotifyTest, UnsealedTxsSizeNotifyFixture)

// The periodic timer is never started here, so every delivery below is event-driven.
BOOST_AUTO_TEST_CASE(InsertIntoEmptyPoolDeliversImmediately)
{
    BOOST_CHECK(delivered.empty());
    auto tx1 = makeTx("n1", /*sealed*/ false);
    BOOST_CHECK(storage->insert(tx1) == bcos::protocol::TransactionStatus::None);
    BOOST_CHECK_EQUAL(delivered.size(), 1U);
    BOOST_CHECK_EQUAL(last(), 1U);

    // Steady state: once the consensus side already knows the pool is non-empty, further
    // inserts do not cost a cross-module call.
    auto tx2 = makeTx("n2", /*sealed*/ false);
    BOOST_CHECK(storage->insert(tx2) == bcos::protocol::TransactionStatus::None);
    BOOST_CHECK_EQUAL(delivered.size(), 1U);
    BOOST_CHECK_EQUAL(storage->size(), 2U);
}

BOOST_AUTO_TEST_CASE(SealAndUnsealDeliverSynchronously)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    storage->insert(tx1);
    BOOST_CHECK_EQUAL(last(), 1U);

    std::vector<bcos::crypto::HashType> hashes{tx1->hash()};
    auto batchHash = bcos::crypto::HashType::generateRandomFixedBytes();
    // prePrepare accepted: the tx now belongs to an in-flight proposal, whose timer
    // PBFTCacheProcessor::resetTimer manages, so the unsealed count must read 0 at once.
    BOOST_CHECK(storage->batchMarkTxs(hashes, /*batchId*/ 1, batchHash, /*sealFlag*/ true));
    BOOST_CHECK_EQUAL(last(), 0U);
    BOOST_CHECK_EQUAL(storage->size(), 1U);

    // proposal dropped by a view change: the tx is pending work again.
    BOOST_CHECK(storage->batchMarkTxs(hashes, /*batchId*/ 1, batchHash, /*sealFlag*/ false));
    BOOST_CHECK_EQUAL(last(), 1U);
}

// The scenario behind the fix: a block with one tx is committed while no other tx is pending.
// After batchRemoveSealedTxs returns (and hence before the scheduler invokes PBFT's commit
// callback), the latest delivery must be 0 and must have been sampled after the removal.
BOOST_AUTO_TEST_CASE(CommittedBlockLeavesNoStaleCount)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    storage->insert(tx1);
    std::vector<bcos::crypto::HashType> hashes{tx1->hash()};
    auto batchHash = bcos::crypto::HashType::generateRandomFixedBytes();
    storage->batchMarkTxs(hashes, /*batchId*/ 1, batchHash, /*sealFlag*/ true);
    BOOST_CHECK_EQUAL(last(), 0U);

    auto deliveriesBefore = delivered.size();
    storage->batchRemoveSealedTxs(/*batchId*/ 1, committedResult(*tx1));
    BOOST_CHECK_EQUAL(storage->size(), 0U);
    BOOST_CHECK_EQUAL(delivered.size(), deliveriesBefore + 1);
    BOOST_CHECK_EQUAL(last(), 0U);

    // A backstop tick after commit still reports 0, never the pre-commit count.
    storage->notifyUnsealedTxsSize();
    BOOST_CHECK_EQUAL(last(), 0U);
}

BOOST_AUTO_TEST_CASE(SealedTxsAreNotCounted)
{
    // A tx imported already sealed (e.g. filled from a peer's proposal) is not pending work.
    auto sealedTx = makeTx("n1", /*sealed*/ true);
    BOOST_CHECK(storage->insert(sealedTx) == bcos::protocol::TransactionStatus::None);
    BOOST_CHECK(delivered.empty());
    BOOST_CHECK_EQUAL(storage->size(), 1U);

    storage->notifyUnsealedTxsSize();
    BOOST_CHECK_EQUAL(delivered.size(), 1U);
    BOOST_CHECK_EQUAL(last(), 0U);
}

BOOST_AUTO_TEST_CASE(RemoveAndClearDeliverZero)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    storage->insert(tx1);
    BOOST_CHECK_EQUAL(last(), 1U);
    storage->remove(tx1->hash());
    BOOST_CHECK_EQUAL(last(), 0U);

    // Back to empty, so the next insert must be delivered again.
    auto tx2 = makeTx("n2", /*sealed*/ false);
    auto deliveriesBefore = delivered.size();
    storage->insert(tx2);
    BOOST_CHECK_EQUAL(delivered.size(), deliveriesBefore + 1);
    BOOST_CHECK_EQUAL(last(), 1U);

    storage->clear();
    BOOST_CHECK_EQUAL(last(), 0U);
}

// Leader path: the sealer's fetch moves txs unsealed -> sealed without going through
// batchMarkTxs, so it must deliver on its own.
BOOST_AUTO_TEST_CASE(LeaderFetchDeliversZero)
{
    auto hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto signatureImpl = std::make_shared<bcos::crypto::Secp256k1Crypto>();
    auto cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(hashImpl, signatureImpl, nullptr);
    auto blockFactory = std::make_shared<bcostars::protocol::BlockFactoryImpl>(cryptoSuite,
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite),
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite),
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite));
    config->setBlockFactory(blockFactory);

    auto tx1 = makeTx("n1", /*sealed*/ false);
    tx1->setImportTime(static_cast<int64_t>(bcos::utcTime()));
    storage->insert(tx1);
    BOOST_CHECK_EQUAL(last(), 1U);

    std::vector<bcos::protocol::TransactionMetaData::Ptr> txsList;
    std::vector<bcos::protocol::TransactionMetaData::Ptr> sysTxsList;
    BOOST_CHECK(storage->batchSealTransactions(txsList, sysTxsList, /*limit*/ 100));
    BOOST_CHECK_EQUAL(txsList.size(), 1U);
    BOOST_CHECK_EQUAL(last(), 0U);
    BOOST_CHECK_EQUAL(storage->size(), 1U);
}

// Sync path: a proposal's txs fetched from a peer are force-submitted as sealed. When the
// local pool already holds one of them unsealed, it moves to sealed, and the batch must
// deliver that net effect before returning.
BOOST_AUTO_TEST_CASE(EnforcedProposalTxsDeliverNetEffect)
{
    auto tx1 = makeTx("n1", /*sealed*/ false);
    storage->insert(tx1);
    BOOST_CHECK_EQUAL(last(), 1U);

    auto proposalTxs = std::make_shared<bcos::protocol::Transactions>();
    proposalTxs->push_back(tx1);
    BOOST_CHECK(storage->batchVerifyAndSubmitTransaction(nullptr, proposalTxs));
    BOOST_CHECK(tx1->sealed());
    BOOST_CHECK_EQUAL(last(), 0U);
    BOOST_CHECK_EQUAL(storage->size(), 1U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
